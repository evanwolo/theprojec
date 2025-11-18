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
    std::uniform_int_distribution<int> ageDist(15, 60);  // Initial population: working age adults
    std::bernoulli_distribution sexDist(0.5);  // 50/50 male/female
    
    for (std::uint32_t i = 0; i < cfg_.population; ++i) {
        Agent a;
        a.id = i;
        a.region = regionDist(rng_);
        a.alive = true;
        
        // Demography: initial population is working-age adults
        a.age = ageDist(rng_);
        a.female = sexDist(rng_);
        
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
        if (!ai.alive) continue;  // Skip dead agents
        
        std::array<double, 4> acc{0, 0, 0, 0};
        
        // Cache agent properties used in inner loop
        const double ai_susceptibility = ai.m_susceptibility;
        const double ai_comm = ai.m_comm;
        
        for (auto jid : ai.neighbors) {
            if (jid >= agents_.size()) continue;  // Safety check
            const auto& aj = agents_[jid];
            if (!aj.alive) continue;  // Skip dead neighbors
            
            double s = similarityGate(ai, aj);
            double lq = languageQuality(ai, aj);
            double comm = 0.5 * (ai_comm + aj.m_comm);
            double weight = stepSize * s * lq * comm * ai_susceptibility;
            
            // Unroll belief dimension loop for better performance
            acc[0] += weight * fastTanh(aj.B[0] - ai.B[0]);
            acc[1] += weight * fastTanh(aj.B[1] - ai.B[1]);
            acc[2] += weight * fastTanh(aj.B[2] - ai.B[2]);
            acc[3] += weight * fastTanh(aj.B[3] - ai.B[3]);
        }
        
        dx[i] = acc;
    }
    
    // Apply updates
    #pragma omp parallel for
    for (std::size_t i = 0; i < n; ++i) {
        if (!agents_[i].alive) continue;  // Skip dead agents
        
        agents_[i].x[0] += dx[i][0];
        agents_[i].x[1] += dx[i][1];
        agents_[i].x[2] += dx[i][2];
        agents_[i].x[3] += dx[i][3];
        
        agents_[i].B[0] = fastTanh(agents_[i].x[0]);
        agents_[i].B[1] = fastTanh(agents_[i].x[1]);
        agents_[i].B[2] = fastTanh(agents_[i].x[2]);
        agents_[i].B[3] = fastTanh(agents_[i].x[3]);

        // Update cached norm
        agents_[i].B_norm_sq = agents_[i].B[0] * agents_[i].B[0] +
                                agents_[i].B[1] * agents_[i].B[1] +
                                agents_[i].B[2] * agents_[i].B[2] +
                                agents_[i].B[3] * agents_[i].B[3];
    }
}

void Kernel::step() {
    updateBeliefs();
    ++generation_;
    
    // Demographic step (if enabled)
    if (cfg_.demographyEnabled) {
        stepDemography();
    }
    
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
            if (!agent.alive) continue;  // Skip dead agents
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
            if (!agent.alive) continue;  // Skip dead agents
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

// ============================================================================
// DEMOGRAPHY IMPLEMENTATION
// ============================================================================

double Kernel::mortalityRate(int age) const {
    // Age-specific mortality (annual probability)
    if (age < 5)   return 0.01;   // 1% child mortality
    if (age < 15)  return 0.001;  // 0.1% youth
    if (age < 50)  return 0.002;  // 0.2% adult
    if (age < 70)  return 0.01;   // 1% middle age
    if (age < 85)  return 0.05;   // 5% elderly
    return 0.15;                   // 15% very old
}

double Kernel::mortalityPerTick(int age) const {
    double annual = mortalityRate(age);
    // Convert annual probability to per-tick: 1 - (1 - p)^(1/ticksPerYear)
    return 1.0 - std::pow(1.0 - annual, 1.0 / cfg_.ticksPerYear);
}

double Kernel::fertilityRateAnnual(int age) const {
    // Age-specific fertility for females (annual probability)
    if (age < 15)  return 0.0;
    if (age < 20)  return 0.05;   // 5% for teens
    if (age < 30)  return 0.12;   // 12% peak fertility
    if (age < 35)  return 0.10;   // 10% 
    if (age < 40)  return 0.05;   // 5% declining
    if (age < 45)  return 0.02;   // 2% late fertility
    return 0.0;
}

double Kernel::fertilityPerTick(int age) const {
    double annual = fertilityRateAnnual(age);
    return 1.0 - std::pow(1.0 - annual, 1.0 / cfg_.ticksPerYear);
}

void Kernel::stepDemography() {
    // Age increment every ticksPerYear ticks
    bool ageIncrement = (generation_ % cfg_.ticksPerYear == 0);
    
    std::vector<std::uint32_t> newBirths;
    std::bernoulli_distribution d(0.5);
    
    // Process deaths and births
    for (auto& agent : agents_) {
        if (!agent.alive) continue;
        
        // Age increment
        if (ageIncrement) {
            agent.age++;
            // Hard cap on age
            if (agent.age > cfg_.maxAgeYears) {
                agent.alive = false;
                continue;
            }
        }
        
        // Mortality
        double pDeath = mortalityPerTick(agent.age);
        d = std::bernoulli_distribution(pDeath);
        if (d(rng_)) {
            agent.alive = false;
            continue;
        }
        
        // Fertility (only for alive females)
        if (agent.female && agent.alive) {
            double pBirth = fertilityPerTick(agent.age);
            
            // Modulate by regional conditions
            const auto& regional_econ = economy_.getRegion(agent.region);
            double hardship = regional_econ.hardship;
            
            // Reduce fertility under extreme hardship
            pBirth *= (0.7 + 0.3 * (1.0 - hardship));
            
            // Carrying capacity pressure
            double regionPop = static_cast<double>(regionIndex_[agent.region].size());
            double capacity = cfg_.regionCapacity;
            if (regionPop > capacity) {
                double pressure = regionPop / capacity;
                pBirth /= pressure;  // Reduce fertility in overpopulated regions
            }
            
            d = std::bernoulli_distribution(pBirth);
            if (d(rng_)) {
                newBirths.push_back(agent.id);
            }
        }
    }
    
    // Create children
    for (auto motherId : newBirths) {
        createChild(motherId);
    }
    
    // Periodically compact dead agents (every 100 ticks to avoid overhead)
    if (generation_ % 100 == 0) {
        compactDeadAgents();
    }
}

void Kernel::createChild(std::uint32_t motherId) {
    if (motherId >= agents_.size()) return;
    
    Agent& mother = agents_[motherId];
    if (!mother.alive) return;
    
    Agent child;
    child.id = static_cast<std::uint32_t>(agents_.size());
    child.alive = true;
    child.age = 0;
    
    // Sex (50/50)
    std::bernoulli_distribution sexDist(0.5);
    child.female = sexDist(rng_);
    
    // Parents
    child.parent_a = static_cast<std::int32_t>(motherId);
    
    // Select father from mother's neighbors or region
    std::int32_t fatherId = -1;
    if (!mother.neighbors.empty()) {
        std::uniform_int_distribution<std::size_t> neighborDist(0, mother.neighbors.size() - 1);
        fatherId = static_cast<std::int32_t>(mother.neighbors[neighborDist(rng_)]);
        // Verify father is alive and male
        if (fatherId >= 0 && fatherId < static_cast<std::int32_t>(agents_.size())) {
            if (!agents_[fatherId].alive || agents_[fatherId].female) {
                fatherId = -1;  // Invalid father
            }
        }
    }
    child.parent_b = fatherId;
    
    // Lineage: inherit from mother (matrilineal) or could use patrilineal/mixed
    child.lineage_id = mother.lineage_id;
    
    // Region: same as mother
    child.region = mother.region;
    
    // Language: inherit from mother
    child.primaryLang = mother.primaryLang;
    child.fluency = 0.5;  // will grow with age/exposure
    
    // Traits: genetic inheritance with mutation
    Agent* father = (fatherId >= 0 && fatherId < static_cast<std::int32_t>(agents_.size())) 
                    ? &agents_[fatherId] : nullptr;
    
    std::normal_distribution<double> mutationNoise(0.0, 0.05);
    auto inherit = [&](double mTrait, double fTrait) -> double {
        double base = father ? 0.5 * (mTrait + fTrait) : mTrait;
        double trait = base + mutationNoise(rng_);
        return std::clamp(trait, 0.0, 1.0);
    };
    
    child.openness = inherit(mother.openness, father ? father->openness : mother.openness);
    child.conformity = inherit(mother.conformity, father ? father->conformity : mother.conformity);
    child.assertiveness = inherit(mother.assertiveness, father ? father->assertiveness : mother.assertiveness);
    child.sociality = inherit(mother.sociality, father ? father->sociality : mother.sociality);
    
    // Beliefs: cultural transmission from parents with noise
    std::normal_distribution<double> beliefNoise(0.0, 0.2);
    for (int k = 0; k < 4; ++k) {
        double baseB = mother.B[k];
        if (father) {
            baseB = 0.5 * (mother.B[k] + father->B[k]);
        }
        child.B[k] = std::clamp(baseB + beliefNoise(rng_), -1.0, 1.0);
        // Convert B to internal state x = atanh(B)
        double B_clamped = std::clamp(child.B[k], -0.99, 0.99);
        child.x[k] = std::atanh(B_clamped);
    }
    child.B_norm_sq = child.B[0]*child.B[0] + child.B[1]*child.B[1] + 
                      child.B[2]*child.B[2] + child.B[3]*child.B[3];
    
    // Module multipliers
    child.m_comm = 1.0;
    child.m_susceptibility = 0.7 + 0.6 * (child.openness - 0.5);
    child.m_mobility = 0.8 + 0.4 * child.sociality;
    
    // Network: connect to mother and some of her neighbors
    child.neighbors.clear();
    child.neighbors.push_back(motherId);
    mother.neighbors.push_back(child.id);  // Reciprocal link
    
    // Inherit some neighbors from mother (family network)
    std::uniform_int_distribution<std::size_t> neighborSelectDist(0, mother.neighbors.size() - 1);
    int neighborCount = std::min(3, static_cast<int>(mother.neighbors.size()));
    for (int i = 0; i < neighborCount; ++i) {
        std::uint32_t neighborId = mother.neighbors[neighborSelectDist(rng_)];
        if (neighborId != child.id && neighborId < agents_.size()) {
            child.neighbors.push_back(neighborId);
            agents_[neighborId].neighbors.push_back(child.id);
        }
    }
    
    // Add to containers
    agents_.push_back(child);
    regionIndex_[child.region].push_back(child.id);
}

void Kernel::compactDeadAgents() {
    // Remove dead agents from regionIndex
    for (auto& region : regionIndex_) {
        region.erase(
            std::remove_if(region.begin(), region.end(), 
                [this](std::uint32_t id) { 
                    return id >= agents_.size() || !agents_[id].alive; 
                }),
            region.end()
        );
    }
    
    // Remove dead agents from neighbor lists
    for (auto& agent : agents_) {
        if (!agent.alive) continue;
        agent.neighbors.erase(
            std::remove_if(agent.neighbors.begin(), agent.neighbors.end(),
                [this](std::uint32_t id) {
                    return id >= agents_.size() || !agents_[id].alive;
                }),
            agent.neighbors.end()
        );
    }
}

