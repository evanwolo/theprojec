#include "kernel/Kernel.h"
#include <cmath>
#include <algorithm>
#include <numeric>
#include <unordered_set>
#include <omp.h>

Kernel::Kernel(const KernelConfig& cfg) : cfg_(cfg), rng_(cfg.seed) {
    reset(cfg);
}

void Kernel::reset(const KernelConfig& cfg) {
    cfg_ = cfg;
    generation_ = 0;
    rng_.seed(cfg.seed);
    psychology_.configure(cfg_.regions, cfg_.seed ^ 0x9E3779B97F4A7C15ULL);
    health_.configure(cfg_.regions, cfg_.seed ^ 0xBF58476D1CE4E5B9ULL);
    initAgents();
    buildSmallWorld();
    
    // Initialize economy with regions and agents
    economy_.init(cfg_.regions, cfg_.population, rng_);
    psychology_.initializeAgents(agents_);
    health_.initializeAgents(agents_);
}

void Kernel::initAgents() {
    agents_.clear();
    agents_.reserve(cfg_.population);
    regionIndex_.assign(cfg_.regions, {});
    
    std::normal_distribution<double> xDist(0.0, 0.75);
    std::normal_distribution<double> traitDist(0.5, 0.15);
    std::uniform_real_distribution<double> uniDist(0.0, 1.0);
    std::uniform_int_distribution<std::uint32_t> regionDist(0, cfg_.regions - 1);
    std::uniform_int_distribution<std::uint8_t> langDist(0, 3);  // 4 base languages
    
    for (std::uint32_t i = 0; i < cfg_.population; ++i) {
        Agent a;
        a.id = i;
        a.region = regionDist(rng_);
        a.primaryLang = langDist(rng_);
        a.fluency = 0.7 + 0.3 * (uniDist(rng_) - 0.5);
        
        // Personality traits
        a.openness = std::clamp(traitDist(rng_), 0.0, 1.0);
        a.conformity = std::clamp(traitDist(rng_), 0.0, 1.0);
        a.assertiveness = std::clamp(traitDist(rng_), 0.0, 1.0);
        a.sociality = std::clamp(traitDist(rng_), 0.0, 1.0);
        
        // Initial beliefs
        for (int k = 0; k < 4; ++k) {
            a.x[k] = xDist(rng_);
            a.B[k] = fastTanh(a.x[k]);
        }
        a.B_norm_sq = a.B[0]*a.B[0] + a.B[1]*a.B[1] + a.B[2]*a.B[2] + a.B[3]*a.B[3];
        
        // Module multipliers (initialized; modules will update)
        a.m_comm = 1.0;
        a.m_susceptibility = 0.7 + 0.6 * (a.openness - 0.5);
        a.m_mobility = 0.8 + 0.4 * a.sociality;
        
        regionIndex_[a.region].push_back(i);
        agents_.push_back(std::move(a));
    }
}

void Kernel::buildSmallWorld() {
    const std::uint32_t N = cfg_.population;
    std::uint32_t K = cfg_.avgConnections;
    if (K % 2) ++K;  // ensure even
    const std::uint32_t halfK = K / 2;
    
    std::uniform_real_distribution<double> uniDist(0.0, 1.0);
    std::uniform_int_distribution<std::uint32_t> nodeDist(0, N - 1);
    
    // Reserve space to avoid reallocations
    for (auto& agent : agents_) {
        agent.neighbors.reserve(K);
    }
    
    // Ring lattice - build only forward edges, avoid duplicates
    for (std::uint32_t i = 0; i < N; ++i) {
        for (std::uint32_t d = 1; d <= halfK; ++d) {
            std::uint32_t j = (i + d) % N;
            agents_[i].neighbors.push_back(j);
            agents_[j].neighbors.push_back(i);
        }
    }
    
    // Rewiring - optimize to avoid repeated set construction
    for (std::uint32_t i = 0; i < N; ++i) {
        // Build set once per agent
        std::unordered_set<std::uint32_t> current(agents_[i].neighbors.begin(), 
                                                    agents_[i].neighbors.end());
        
        for (std::uint32_t d = 1; d <= halfK; ++d) {
            if (uniDist(rng_) < cfg_.rewireProb) {
                std::uint32_t oldJ = (i + d) % N;
                
                // Only rewire if edge still exists
                if (current.count(oldJ) == 0) continue;
                
                // Find new target (with iteration limit to avoid infinite loop)
                std::uint32_t newJ;
                int attempts = 0;
                const int maxAttempts = N * 2;
                do {
                    newJ = nodeDist(rng_);
                    if (++attempts > maxAttempts) break;
                } while (newJ == i || current.count(newJ));
                
                if (attempts > maxAttempts) continue;
                
                // Remove old edge from both sides
                auto& niNbrs = agents_[i].neighbors;
                auto& njNbrs = agents_[oldJ].neighbors;
                niNbrs.erase(std::remove(niNbrs.begin(), niNbrs.end(), oldJ), niNbrs.end());
                njNbrs.erase(std::remove(njNbrs.begin(), njNbrs.end(), i), njNbrs.end());
                current.erase(oldJ);
                
                // Add new edge
                agents_[i].neighbors.push_back(newJ);
                agents_[newJ].neighbors.push_back(i);
                current.insert(newJ);
            }
        }
    }
    
    // Deduplicate and remove self-loops (final cleanup)
    for (auto& agent : agents_) {
        std::unordered_set<std::uint32_t> unique;
        std::vector<std::uint32_t> cleaned;
        cleaned.reserve(agent.neighbors.size());
        for (auto nid : agent.neighbors) {
            if (nid != agent.id && unique.insert(nid).second) {
                cleaned.push_back(nid);
            }
        }
        agent.neighbors = std::move(cleaned);
    }
}

void Kernel::updateBeliefs() {
    // Compute deltas in parallel-friendly way
    std::vector<std::array<double, 4>> dx(agents_.size());
    
    const std::size_t n = agents_.size();
    const double stepSize = cfg_.stepSize;
    
    #pragma omp parallel for schedule(dynamic)
    for (std::size_t i = 0; i < n; ++i) {
        const auto& ai = agents_[i];
        std::array<double, 4> acc{0, 0, 0, 0};
        
        // Cache agent properties used in inner loop
        const double ai_susceptibility = ai.m_susceptibility;
        const double ai_comm = ai.m_comm;
        const double base_weight = stepSize * ai_susceptibility;
        const std::uint8_t ai_lang = ai.primaryLang;
        const double ai_fluency = ai.fluency;
        
        // Cache ai beliefs for inner loop
        const double ai_B0 = ai.B[0];
        const double ai_B1 = ai.B[1];
        const double ai_B2 = ai.B[2];
        const double ai_B3 = ai.B[3];
        
        for (auto jid : ai.neighbors) {
            const auto& aj = agents_[jid];
            
            double s = similarityGate(ai, aj);
            
            // Inline and optimize languageQuality
            double lq;
            if (ai_lang == aj.primaryLang) {
                lq = 0.5 * (ai_fluency + aj.fluency);
            } else {
                lq = 0.1;
            }
            
            double comm = 0.5 * (ai_comm + aj.m_comm);
            double weight = base_weight * s * lq * comm;
            
            // Unroll belief dimension loop for better performance
            acc[0] += weight * fastTanh(aj.B[0] - ai_B0);
            acc[1] += weight * fastTanh(aj.B[1] - ai_B1);
            acc[2] += weight * fastTanh(aj.B[2] - ai_B2);
            acc[3] += weight * fastTanh(aj.B[3] - ai_B3);
        }
        
        dx[i] = acc;
    }
    
    // Apply updates
    #pragma omp parallel for
    for (std::size_t i = 0; i < n; ++i) {
        agents_[i].x[0] += dx[i][0];
        agents_[i].x[1] += dx[i][1];
        agents_[i].x[2] += dx[i][2];
        agents_[i].x[3] += dx[i][3];
        
        agents_[i].B[0] = fastTanh(agents_[i].x[0]);
        agents_[i].B[1] = fastTanh(agents_[i].x[1]);
        agents_[i].B[2] = fastTanh(agents_[i].x[2]);
        agents_[i].B[3] = fastTanh(agents_[i].x[3]);

        // Update cached norm
        const double B0 = agents_[i].B[0];
        const double B1 = agents_[i].B[1];
        const double B2 = agents_[i].B[2];
        const double B3 = agents_[i].B[3];
        agents_[i].B_norm_sq = B0 * B0 + B1 * B1 + B2 * B2 + B3 * B3;
    }
}

void Kernel::step() {
    updateBeliefs();
    ++generation_;
    
    // Update economy every 10 ticks (reduce overhead)
    if (generation_ % 10 == 0) {
        // Build population counts and belief centroids per region
        std::vector<std::uint32_t> region_populations(cfg_.regions, 0);
        std::vector<std::array<double, 4>> region_belief_centroids(cfg_.regions);
        
        // Initialize centroids to zero
        for (auto& centroid : region_belief_centroids) {
            centroid = {0.0, 0.0, 0.0, 0.0};
        }
        
        // Accumulate beliefs per region
        for (const auto& agent : agents_) {
            region_populations[agent.region]++;
            region_belief_centroids[agent.region][0] += agent.B[0];
            region_belief_centroids[agent.region][1] += agent.B[1];
            region_belief_centroids[agent.region][2] += agent.B[2];
            region_belief_centroids[agent.region][3] += agent.B[3];
        }
        
        // Compute averages
        for (std::uint32_t r = 0; r < cfg_.regions; ++r) {
            if (region_populations[r] > 0) {
                const double inv_pop = 1.0 / region_populations[r];
                region_belief_centroids[r][0] *= inv_pop;
                region_belief_centroids[r][1] *= inv_pop;
                region_belief_centroids[r][2] *= inv_pop;
                region_belief_centroids[r][3] *= inv_pop;
            }
        }
        
        economy_.update(region_populations, region_belief_centroids, agents_, generation_);
        
        // Apply economic feedback to agent beliefs and susceptibility
        for (auto& agent : agents_) {
            const auto& regional_econ = economy_.getRegion(agent.region);
            const auto& agent_econ = economy_.getAgentEconomy(agent.id);
            
            // Hardship increases susceptibility to radical beliefs
            agent.m_susceptibility = 0.7 + 0.6 * (agent.openness - 0.5);
            agent.m_susceptibility *= (1.0 + regional_econ.hardship);
            agent.m_susceptibility = std::clamp(agent.m_susceptibility, 0.4, 2.0);
            
            // Economic conditions shape political thought
            double econ_pressure = 0.001;  // gentle pressure each tick
            
            // High personal hardship → reject authority, demand equality
            if (agent_econ.hardship > 0.5) {
                agent.B[0] -= econ_pressure * agent_econ.hardship;  // Authority → Liberty
                agent.B[2] -= econ_pressure * agent_econ.hardship;  // Hierarchy → Equality
            }
            
            // High inequality in region → push toward equality
            if (regional_econ.inequality > 0.4) {
                agent.B[2] -= econ_pressure * regional_econ.inequality;  // Hierarchy → Equality
            }
            
            // Personal wealth → support hierarchy and authority
            if (agent_econ.wealth > 2.0) {
                agent.B[0] += econ_pressure * 0.5;  // Liberty → Authority
                agent.B[2] += econ_pressure * 0.5;  // Equality → Hierarchy
            }
            
            // Economic system shapes ideology
            if (regional_econ.economic_system == "market") {
                // Market economy → favor liberty, accept hierarchy
                agent.B[0] -= econ_pressure * 0.3;  // → Liberty
                agent.B[2] += econ_pressure * 0.2;  // → Hierarchy
            } else if (regional_econ.economic_system == "planned") {
                // Planned economy → favor authority, demand equality
                agent.B[0] += econ_pressure * 0.3;  // → Authority
                agent.B[2] -= econ_pressure * 0.3;  // → Equality
            } else if (regional_econ.economic_system == "feudal") {
                // Feudal system → tradition and hierarchy dominate
                agent.B[1] += econ_pressure * 0.4;  // → Tradition
                agent.B[2] += econ_pressure * 0.4;  // → Hierarchy
            } else if (regional_econ.economic_system == "cooperative") {
                // Cooperative → equality and local autonomy
                agent.B[2] -= econ_pressure * 0.3;  // → Equality
                agent.B[0] -= econ_pressure * 0.2;  // → Liberty
            }
            
            // Low welfare → reject tradition (demand change)
            if (regional_econ.welfare < 0.5) {
                agent.B[1] -= econ_pressure * (0.5 - regional_econ.welfare);  // Tradition → Progress
            }
            
            // Keep beliefs in [-1, 1] range
            for (int d = 0; d < 4; ++d) {
                agent.B[d] = std::clamp(agent.B[d], -1.0, 1.0);
            }
        }
    }

    // Update health and psychology every tick using latest economic signals
    health_.updateAgents(agents_, economy_, generation_);
    psychology_.updateAgents(agents_, economy_, generation_);
}

void Kernel::stepN(int n) {
    for (int i = 0; i < n; ++i) {
        step();
    }
}

Kernel::Metrics Kernel::computeMetrics() const {
    Metrics m;
    
    // Compute region centroids
    std::vector<std::array<double, 4>> centroids(cfg_.regions);
    std::vector<int> counts(cfg_.regions, 0);
    
    for (std::uint32_t r = 0; r < cfg_.regions; ++r) {
        std::array<double, 4> c{0, 0, 0, 0};
        for (auto id : regionIndex_[r]) {
            const auto& B = agents_[id].B;
            c[0] += B[0];
            c[1] += B[1];
            c[2] += B[2];
            c[3] += B[3];
        }
        int n = static_cast<int>(regionIndex_[r].size());
        if (n > 0) {
            const double inv_n = 1.0 / n;
            c[0] *= inv_n;
            c[1] *= inv_n;
            c[2] *= inv_n;
            c[3] *= inv_n;
        }
        centroids[r] = c;
        counts[r] = n;
    }
    
    // Pairwise distances between centroids
    std::vector<double> dists;
    dists.reserve((cfg_.regions * (cfg_.regions - 1)) / 2);  // Upper bound
    for (std::uint32_t i = 0; i < cfg_.regions; ++i) {
        if (counts[i] == 0) continue;
        for (std::uint32_t j = i + 1; j < cfg_.regions; ++j) {
            if (counts[j] == 0) continue;
            // Unroll 4D distance calculation
            const double d0 = centroids[i][0] - centroids[j][0];
            const double d1 = centroids[i][1] - centroids[j][1];
            const double d2 = centroids[i][2] - centroids[j][2];
            const double d3 = centroids[i][3] - centroids[j][3];
            dists.push_back(std::sqrt(d0*d0 + d1*d1 + d2*d2 + d3*d3));
        }
    }
    
    if (!dists.empty()) {
        const double n = static_cast<double>(dists.size());
        m.polarizationMean = std::accumulate(dists.begin(), dists.end(), 0.0) / n;
        double sq = 0.0;
        for (double v : dists) {
            const double diff = v - m.polarizationMean;
            sq += diff * diff;
        }
        m.polarizationStd = std::sqrt(sq / n);
    }
    
    // Average traits
    m.avgOpenness = 0.0;
    m.avgConformity = 0.0;
    for (const auto& a : agents_) {
        m.avgOpenness += a.openness;
        m.avgConformity += a.conformity;
    }
    m.avgOpenness /= agents_.size();
    m.avgConformity /= agents_.size();
    
    // Economy metrics
    m.globalWelfare = economy_.globalWelfare();
    m.globalInequality = economy_.globalInequality();
    m.globalHardship = economy_.globalHardship();
    
    return m;
}
