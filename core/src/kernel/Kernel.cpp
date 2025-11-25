#include "kernel/Kernel.h"
#include "modules/Culture.h"
#include <cmath>
#include <algorithm>
#include <numeric>
#include <unordered_set>
#include <omp.h>

Kernel::Kernel(const KernelConfig& cfg) : cfg_(cfg), rng_(cfg.seed) {
    // Validate demographic parameters
    if (cfg.demographyEnabled) {
        if (cfg.ticksPerYear <= 0) {
            throw std::invalid_argument("ticksPerYear must be > 0 (got " + 
                                      std::to_string(cfg.ticksPerYear) + ")");
        }
        if (cfg.maxAgeYears <= 0) {
            throw std::invalid_argument("maxAgeYears must be > 0 (got " + 
                                      std::to_string(cfg.maxAgeYears) + ")");
        }
        if (cfg.regionCapacity <= 0) {
            throw std::invalid_argument("regionCapacity must be > 0 (got " + 
                                      std::to_string(cfg.regionCapacity) + ")");
        }
        
        // Validate mortality/fertility curve ranges would be checked at runtime
        // (curves are computed algorithmically, so bounds are implicit)
    }
    
    reset(cfg);
}

void Kernel::reset(const KernelConfig& cfg) {
    cfg_ = cfg;
    generation_ = 0;
    rng_.seed(cfg.seed);
    psychology_.configure(cfg_.regions, cfg_.seed ^ 0x9E3779B97F4A7C15ULL);
    health_.configure(cfg_.regions, cfg_.seed ^ 0xBF58476D1CE4E5B9ULL);
    mean_field_.configure(cfg_.regions);
    
    // Initialize economy FIRST so we have region coordinates
    economy_.init(cfg_.regions, cfg_.population, rng_, cfg_.startCondition);
    
    initAgents();
    buildSmallWorld();
    
    // Assign languages based on region coordinates (after economy init)
    assignLanguagesByGeography();
    
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
    
    // Realistic age distribution - approximates demographic pyramid
    // Age brackets: [0-15), [15-30), [30-50), [50-70), [70-90]
    std::vector<double> age_boundaries = {0.0, 15.0, 30.0, 50.0, 70.0, 90.0};
    std::vector<double> age_weights = {0.20, 0.28, 0.26, 0.18, 0.08};
    std::piecewise_constant_distribution<double> ageDist(
        age_boundaries.begin(), age_boundaries.end(),
        age_weights.begin()
    );
    std::bernoulli_distribution sexDist(0.5);  // 50/50 male/female
    
    for (std::uint32_t i = 0; i < cfg_.population; ++i) {
        Agent a;
        a.id = i;
        a.region = regionDist(rng_);
        a.alive = true;
        
        // Demography: realistic age distribution
        a.age = static_cast<int>(ageDist(rng_));
        a.female = sexDist(rng_);
        
        // Language will be assigned after economy init (in assignLanguagesByGeography)
        a.primaryLang = 0;
        a.dialect = 0;
        a.fluency = 0.7 + 0.3 * (uniDist(rng_) - 0.5);
        
        // Personality traits - all drawn from same distribution
        // Leadership emerges from network position + traits, not predetermined
        a.openness = std::clamp(traitDist(rng_), 0.0, 1.0);
        a.conformity = std::clamp(traitDist(rng_), 0.0, 1.0);
        a.assertiveness = std::clamp(traitDist(rng_), 0.0, 1.0);
        a.sociality = std::clamp(traitDist(rng_), 0.0, 1.0);
        
        // NOTE: Removed hardcoded "every 100th agent is a leader" logic
        // High-assertiveness individuals will naturally emerge from the trait distribution
        // (traitDist has mean 0.5, std 0.15, so ~2.5% will have assertiveness > 0.8 naturally)
        
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

void Kernel::assignLanguagesByGeography() {
    // EMERGENT LANGUAGE ZONES: Fuzzy boundaries with gradients and minority pockets
    // Language families have "cores" but influence fades with distance
    // Border regions have mixed languages; isolated areas may develop differently
    
    std::uniform_real_distribution<double> uniDist(0.0, 1.0);
    std::uniform_int_distribution<std::uint8_t> langDist(0, 3);
    std::normal_distribution<double> noiseDist(0.0, 0.15);  // boundary noise
    
    // Language family centers (not hard quadrant boundaries)
    // Centers can shift slightly each simulation for variety
    double western_cx = 0.25 + noiseDist(rng_) * 0.1;
    double western_cy = 0.75 + noiseDist(rng_) * 0.1;
    double eastern_cx = 0.75 + noiseDist(rng_) * 0.1;
    double eastern_cy = 0.75 + noiseDist(rng_) * 0.1;
    double northern_cx = 0.25 + noiseDist(rng_) * 0.1;
    double northern_cy = 0.25 + noiseDist(rng_) * 0.1;
    double southern_cx = 0.75 + noiseDist(rng_) * 0.1;
    double southern_cy = 0.25 + noiseDist(rng_) * 0.1;
    
    // Compute language for each region based on distance to language centers
    std::vector<std::uint8_t> regionLang(cfg_.regions);
    std::vector<std::uint8_t> regionDialect(cfg_.regions);
    std::vector<double> regionLangStrength(cfg_.regions);  // how "strongly" region speaks its language
    
    for (std::uint32_t r = 0; r < cfg_.regions; ++r) {
        const auto& region = economy_.getRegion(r);
        double x = region.x;
        double y = region.y;
        
        // Distance to each language center (with noise for natural boundaries)
        double noise_x = noiseDist(rng_) * 0.1;
        double noise_y = noiseDist(rng_) * 0.1;
        double px = x + noise_x;
        double py = y + noise_y;
        
        double dist_western = std::sqrt((px - western_cx) * (px - western_cx) + (py - western_cy) * (py - western_cy));
        double dist_eastern = std::sqrt((px - eastern_cx) * (px - eastern_cx) + (py - eastern_cy) * (py - eastern_cy));
        double dist_northern = std::sqrt((px - northern_cx) * (px - northern_cx) + (py - northern_cy) * (py - northern_cy));
        double dist_southern = std::sqrt((px - southern_cx) * (px - southern_cx) + (py - southern_cy) * (py - southern_cy));
        
        // Find minimum distance (dominant language)
        std::uint8_t lang = 0;
        double min_dist = dist_western;
        if (dist_eastern < min_dist) { min_dist = dist_eastern; lang = 1; }
        if (dist_northern < min_dist) { min_dist = dist_northern; lang = 2; }
        if (dist_southern < min_dist) { min_dist = dist_southern; lang = 3; }
        
        regionLang[r] = lang;
        
        // Language strength: closer to center = stronger identity
        // Regions far from any center are more linguistically mixed
        regionLangStrength[r] = std::max(0.3, 1.0 - min_dist * 1.5);
        
        // Dialect based on exact position (continuous variation)
        double dialectPos = (x + y * 1.3 + region.endowments[0] * 0.2) / 2.5;  // geography + resources affect dialect
        regionDialect[r] = static_cast<std::uint8_t>(std::min(9.0, std::max(0.0, dialectPos * 10.0)));
    }
    
    // Assign languages to agents based on their region
    for (auto& agent : agents_) {
        if (!agent.alive) continue;
        
        std::uint8_t baseLang = regionLang[agent.region];
        std::uint8_t baseDialect = regionDialect[agent.region];
        double langStrength = regionLangStrength[agent.region];
        
        // EMERGENT LANGUAGE ASSIGNMENT: Minority language probability varies by region strength
        // Border regions (low strength) have more language diversity
        double minority_chance = (1.0 - langStrength) * 0.3;  // 0-21% chance based on distance from core
        
        // Agent's mobility and openness increase chance of speaking non-regional language
        minority_chance += agent.m_mobility * 0.05 + agent.openness * 0.05;
        minority_chance = std::min(0.4, minority_chance);  // cap at 40%
        
        if (uniDist(rng_) < minority_chance) {
            // More likely to speak neighboring language than distant one
            // Weight by inverse distance to other language centers
            agent.primaryLang = langDist(rng_);  // simplified: random for now
            agent.dialect = static_cast<std::uint8_t>(uniDist(rng_) * 10);
        } else {
            agent.primaryLang = baseLang;
            // Dialect variation: stronger language regions have less variation
            int maxVariation = static_cast<int>(3 * (1.0 - langStrength * 0.5));  // 1-3
            int dialectVariation = static_cast<int>(uniDist(rng_) * (maxVariation * 2 + 1)) - maxVariation;
            agent.dialect = static_cast<std::uint8_t>(std::clamp(
                static_cast<int>(baseDialect) + dialectVariation, 0, 9));
        }
    }
}

void Kernel::updateBeliefs() {
    if (cfg_.useMeanField) {
        // **MEAN FIELD APPROXIMATION**: O(N) complexity, decoupled from network density
        // Compute regional fields once
        mean_field_.computeFields(agents_, regionIndex_);
        
        // Update each agent based on regional field
        const std::size_t n = agents_.size();
        const double stepSize = cfg_.stepSize;
        
        #pragma omp parallel for schedule(dynamic)
        for (std::size_t i = 0; i < n; ++i) {
            auto& agent = agents_[i];
            if (!agent.alive) continue;
            
            // Get regional field
            const auto& field = mean_field_.getRegionalField(agent.region);
            double field_strength = mean_field_.getFieldStrength(agent.region);
            
            // Compute influence from field
            double weight = stepSize * field_strength * agent.m_comm * agent.m_susceptibility;
            
            // Update beliefs toward field
            std::array<double, 4> dx;
            dx[0] = weight * fastTanh(field[0] - agent.B[0]);
            dx[1] = weight * fastTanh(field[1] - agent.B[1]);
            dx[2] = weight * fastTanh(field[2] - agent.B[2]);
            dx[3] = weight * fastTanh(field[3] - agent.B[3]);
            
            // Apply updates
            agent.x[0] += dx[0];
            agent.x[1] += dx[1];
            agent.x[2] += dx[2];
            agent.x[3] += dx[3];
            
            agent.B[0] = fastTanh(agent.x[0]);
            agent.B[1] = fastTanh(agent.x[1]);
            agent.B[2] = fastTanh(agent.x[2]);
            agent.B[3] = fastTanh(agent.x[3]);

            // Update cached norm
            agent.B_norm_sq = agent.B[0] * agent.B[0] +
                             agent.B[1] * agent.B[1] +
                             agent.B[2] * agent.B[2] +
                             agent.B[3] * agent.B[3];
        }
    } else {
        // **ORIGINAL PAIRWISE UPDATES**: O(N·k) complexity
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
}

void Kernel::step() {
    updateBeliefs();
    ++generation_;
    
    // Demographic step (if enabled)
    if (cfg_.demographyEnabled) {
        stepDemography();
        
        // Migration step (every 10 ticks to reduce overhead)
        if (generation_ % 10 == 0) {
            stepMigration();
        }
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
            
            // EMERGENT BELIEF EVOLUTION: Economic experience MAY influence beliefs
            // but the direction depends on personality, not predetermined mappings
            
            // Base pressure is very small - beliefs change slowly
            double base_pressure = 0.0005;
            
            // Openness determines how much economic experience affects beliefs
            double experience_weight = agent.openness * base_pressure;
            
            // Personal hardship creates pressure for SOME change, direction varies by personality
            if (agent_econ.hardship > 0.3) {
                double hardship_pressure = experience_weight * agent_econ.hardship;
                
                // High conformity → blame self/accept system, low conformity → blame system
                if (agent.conformity < 0.4) {
                    // Non-conformist: hardship → question authority/hierarchy
                    agent.B[0] -= hardship_pressure * (0.5 - agent.conformity);
                    agent.B[2] -= hardship_pressure * (0.5 - agent.conformity);
                } else if (agent.conformity > 0.6) {
                    // Conformist: hardship → support stronger authority for stability
                    agent.B[0] += hardship_pressure * (agent.conformity - 0.5);
                }
                // Middle conformity: no systematic shift
            }
            
            // Wealth influences beliefs ONLY through lived experience, modulated by traits
            double relative_wealth = agent_econ.wealth / std::max(0.5, regional_econ.welfare);
            if (relative_wealth > 2.0 && agent.openness < 0.5) {
                // Wealthy + low openness → rationalize current system (slight hierarchy support)
                agent.B[2] += experience_weight * 0.3;
            } else if (relative_wealth < 0.5 && agent.assertiveness > 0.6) {
                // Poor + assertive → demand change (slight equality push)
                agent.B[2] -= experience_weight * 0.3;
            }
            // NOTE: Most agents (moderate traits) have no systematic wealth→belief pressure
            
            // Regional conditions create shared experiences, but response varies
            if (regional_econ.welfare < 0.5 && agent.openness > 0.6) {
                // Low welfare + high openness → open to change (progress over tradition)
                agent.B[1] -= experience_weight * (0.5 - regional_econ.welfare);
            }
            
            // NO SYSTEM-BASED BELIEF FORCING
            // Economic systems don't automatically push beliefs in predetermined directions
            // Beliefs shaped by actual experiences, social influence, and personality
            
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
    // Base age-specific mortality (annual probability)
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

// Region-specific mortality rate (modulated by development and welfare)
double Kernel::mortalityPerTick(int age, std::uint32_t region_id) const {
    double base_annual = mortalityRate(age);
    
    // Regional modulation
    const auto& regional_econ = economy_.getRegion(region_id);
    double development_factor = 1.0 / (1.0 + regional_econ.development * 0.15);  // Better development → lower mortality
    double welfare_factor = 1.0 / std::max(0.5, regional_econ.welfare);  // Better welfare → lower mortality
    
    // Child mortality especially sensitive to development
    if (age < 5) {
        development_factor = 1.0 / (1.0 + regional_econ.development * 0.3);
    }
    
    double adjusted_annual = base_annual * development_factor * welfare_factor;
    adjusted_annual = std::clamp(adjusted_annual, 0.0001, 0.5);  // Keep reasonable bounds
    
    return 1.0 - std::pow(1.0 - adjusted_annual, 1.0 / cfg_.ticksPerYear);
}

double Kernel::fertilityRateAnnual(int age) const {
    // Base age-specific fertility for females (annual probability)
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

// Region and agent-specific fertility rate (modulated by culture, development, and wealth)
double Kernel::fertilityPerTick(int age, std::uint32_t region_id, const Agent& agent,
                                const std::array<double, 4>& region_beliefs) const {
    double base_annual = fertilityRateAnnual(age);
    if (base_annual == 0.0) return 0.0;
    
    // Cultural modulation based on regional beliefs
    // Tradition-Progress axis (B[1]): Tradition (+1) → higher fertility, Progress (-1) → lower fertility
    double tradition = region_beliefs[1];
    double tradition_factor = 1.0 + tradition * 0.3;  // ±30% based on cultural values
    
    // Regional development → demographic transition (lower fertility with higher development)
    const auto& regional_econ = economy_.getRegion(region_id);
    double development_factor = 1.0 / (1.0 + regional_econ.development * 0.15);  // Higher development → lower fertility
    
    // Socioeconomic status: wealthier agents have fewer children (quality-quantity tradeoff)
    const auto& agent_econ = economy_.getAgentEconomy(agent.id);
    double wealth_factor = 1.0;
    if (regional_econ.development > 0.5) {  // Demographic transition only in developed regions
        // Normalize wealth relative to regional average
        double avg_wealth = 1.0;  // Agents start at ~1.0 wealth
        double relative_wealth = std::clamp(agent_econ.wealth / avg_wealth, 0.5, 3.0);
        wealth_factor = 1.5 / relative_wealth;  // Richer → fewer children (inverse relationship)
    }
    
    // Delayed childbearing in high-development regions (shift peak age)
    double age_shift_factor = 1.0;
    if (regional_econ.development > 1.0 && age < 25) {
        // Reduce teen/early-20s fertility in developed regions
        age_shift_factor = 0.5 + 0.5 * (age / 25.0);
    }
    
    double adjusted_annual = base_annual * tradition_factor * development_factor * 
                            wealth_factor * age_shift_factor;
    adjusted_annual = std::clamp(adjusted_annual, 0.0, 0.25);  // Cap at 25% annual
    
    return 1.0 - std::pow(1.0 - adjusted_annual, 1.0 / cfg_.ticksPerYear);
}

void Kernel::stepDemography() {
    // Age increment every ticksPerYear ticks
    bool ageIncrement = (generation_ % cfg_.ticksPerYear == 0);
    
    // Compute regional belief centroids for cultural modulation
    std::vector<std::array<double, 4>> region_belief_centroids(cfg_.regions);
    std::vector<std::uint32_t> region_populations(cfg_.regions, 0);
    
    for (auto& centroid : region_belief_centroids) {
        centroid.fill(0.0);
    }
    
    for (const auto& agent : agents_) {
        if (agent.alive) {
            region_populations[agent.region]++;
            region_belief_centroids[agent.region][0] += agent.B[0];
            region_belief_centroids[agent.region][1] += agent.B[1];
            region_belief_centroids[agent.region][2] += agent.B[2];
            region_belief_centroids[agent.region][3] += agent.B[3];
        }
    }
    
    for (std::size_t r = 0; r < cfg_.regions; ++r) {
        if (region_populations[r] > 0) {
            double inv_pop = 1.0 / region_populations[r];
            region_belief_centroids[r][0] *= inv_pop;
            region_belief_centroids[r][1] *= inv_pop;
            region_belief_centroids[r][2] *= inv_pop;
            region_belief_centroids[r][3] *= inv_pop;
        }
    }
    
    std::vector<std::uint32_t> newBirths;
    std::uniform_real_distribution<double> uniform_01(0.0, 1.0);
    int death_count = 0;
    int birth_count = 0;
    
    // Process deaths and births
    for (auto& agent : agents_) {
        if (!agent.alive) continue;
        
        // Age increment
        if (ageIncrement) {
            agent.age++;
            // Hard cap on age
            if (agent.age > cfg_.maxAgeYears) {
                agent.alive = false;
                event_log_.logDeath(generation_, agent.id, agent.region, agent.age);
                death_count++;
                continue;
            }
        }
        
        // Mortality (region-specific) - use uniform distribution for reliability
        double pDeath = mortalityPerTick(agent.age, agent.region);
        if (uniform_01(rng_) < pDeath) {
            agent.alive = false;
            event_log_.logDeath(generation_, agent.id, agent.region, agent.age);
            death_count++;
            continue;
        }
        
        // Fertility (only for alive females)
        if (agent.female && agent.alive) {
            // Use region and agent-specific fertility rate (includes cultural, development, and wealth factors)
            double pBirth = fertilityPerTick(agent.age, agent.region, agent, 
                                            region_belief_centroids[agent.region]);
            
            // Additional modulation by hardship and carrying capacity
            const auto& regional_econ = economy_.getRegion(agent.region);
            double hardship = regional_econ.hardship;
            
            // Reduce fertility under extreme hardship
            pBirth *= (0.7 + 0.3 * (1.0 - hardship));
            
            // Carrying capacity pressure
            double regionPop = static_cast<double>(region_populations[agent.region]);
            double capacity = cfg_.regionCapacity;
            if (regionPop > capacity) {
                double pressure = regionPop / capacity;
                pBirth /= pressure;  // Reduce fertility in overpopulated regions
            }
            
            if (uniform_01(rng_) < pBirth) {
                newBirths.push_back(agent.id);
                birth_count++;
            }
        }
    }
    
    // Create children
    for (auto motherId : newBirths) {
        createChild(motherId);
    }
    
    // Debug output for demographic tracking (optional - can be removed in production)
    if (death_count > 0 || birth_count > 0) {
        // Output is visible when running with verbose logging
        // fprintf(stderr, "[Demo] Gen %llu: %d births, %d deaths (pop: %zu)\n", 
        //         generation_, birth_count, death_count, agents_.size());
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
    
    // Language: inherit from mother, dialect may drift toward regional norm
    child.primaryLang = mother.primaryLang;
    // Dialect: 80% inherit mother's, 20% drift toward region's dialect
    const auto& region = economy_.getRegion(mother.region);
    // Compute region's base dialect from coordinates
    double x = region.x;
    double y = region.y;
    std::uint8_t lang = mother.primaryLang;
    double qx = (lang == 0 || lang == 2) ? x : (1.0 - x);
    double qy = (lang == 0 || lang == 1) ? (1.0 - y) : y;
    double dialectPos = (qx + qy) / 2.0;
    std::uint8_t regionDialect = static_cast<std::uint8_t>(std::min(9.0, dialectPos * 20.0));
    std::bernoulli_distribution dialectDrift(0.2);
    child.dialect = dialectDrift(rng_) ? regionDialect : mother.dialect;
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
    
    // Register with economy module
    economy_.addAgent(child.id, child.region, rng_);
    
    // Log birth event
    event_log_.logBirth(generation_, child.id, child.region, motherId);
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

void Kernel::stepMigration() {
    // Migration decisions: young adults with high hardship + high mobility move to better regions
    // This creates rural→urban, periphery→core flows
    
    // Compute regional attractiveness scores
    std::vector<double> region_attractiveness(cfg_.regions, 0.0);
    std::vector<std::uint32_t> region_populations(cfg_.regions, 0);
    
    for (const auto& agent : agents_) {
        if (agent.alive) {
            region_populations[agent.region]++;
        }
    }
    
    for (std::size_t r = 0; r < cfg_.regions; ++r) {
        const auto& regional_econ = economy_.getRegion(r);
        
        // Attractiveness = welfare - hardship + development - crowding
        double welfare_pull = regional_econ.welfare;
        double hardship_push = -regional_econ.hardship * 2.0;  // Hardship is strong push
        double development_pull = regional_econ.development * 0.2;
        
        // Crowding penalty (reduces attractiveness when over capacity)
        double crowding = 0.0;
        if (region_populations[r] > cfg_.regionCapacity) {
            crowding = -(region_populations[r] / cfg_.regionCapacity - 1.0) * 0.5;
        }
        
        region_attractiveness[r] = welfare_pull + hardship_push + development_pull + crowding;
    }
    
    // EMERGENT MIGRATION CANDIDATES: Anyone can migrate, but propensity varies by circumstance
    // Age, family status, skills, and economic situation all affect likelihood
    std::vector<std::uint32_t> migration_candidates;
    for (std::size_t i = 0; i < agents_.size(); ++i) {
        const auto& agent = agents_[i];
        if (!agent.alive) continue;
        
        // Base mobility affected by age (young and old migrate less frequently, but CAN migrate)
        double age_mobility = 1.0;
        if (agent.age < 18) age_mobility = 0.1 + agent.age * 0.05; // children rarely migrate alone
        else if (agent.age > 60) age_mobility = std::max(0.1, 1.0 - (agent.age - 60) * 0.02); // elderly migrate less
        
        // Network ties reduce mobility (people with many connections are "rooted")
        double network_mobility = 1.0 - std::min(0.5, agent.neighbors.size() * 0.02);
        
        // Effective mobility combines traits with situational factors
        double effective_mobility = agent.m_mobility * age_mobility * network_mobility;
        
        // Everyone with any mobility can be a candidate (threshold varies)
        if (effective_mobility > 0.3) {
            migration_candidates.push_back(i);
        }
    }
    
    // Process migration decisions (stochastic, only a fraction migrate each tick)
    std::uniform_real_distribution<double> prob_dist(0.0, 1.0);
    std::uniform_int_distribution<std::uint32_t> region_dist(0, cfg_.regions - 1);
    
    for (auto agent_id : migration_candidates) {
        auto& agent = agents_[agent_id];
        std::uint32_t origin = agent.region;
        
        // Migration propensity based on origin hardship and agent mobility
        const auto& origin_econ = economy_.getAgentEconomy(agent_id);
        double push_factor = origin_econ.hardship * agent.m_mobility;
        
        // Base migration rate: 1% per tick for high-hardship, high-mobility agents
        double migration_prob = push_factor * 0.01;
        
        if (prob_dist(rng_) < migration_prob) {
            // Find attractive destination (biased random choice)
            std::uint32_t destination = origin;
            double best_gain = 0.0;
            
            // Sample a few random regions and pick the most attractive
            for (int attempt = 0; attempt < 5; ++attempt) {
                std::uint32_t candidate = region_dist(rng_);
                if (candidate == origin) continue;
                
                double gain = region_attractiveness[candidate] - region_attractiveness[origin];
                if (gain > best_gain) {
                    best_gain = gain;
                    destination = candidate;
                }
            }
            
            // EMERGENT MIGRATION THRESHOLD: Decision varies by personality and circumstances
            // Risk-tolerant agents migrate for smaller gains; risk-averse need bigger incentive
            double personal_threshold = 0.1 + (1.0 - agent.openness) * 0.3 + agent.conformity * 0.2;
            // Desperate agents (high hardship) lower their threshold
            personal_threshold *= (1.0 - origin_econ.hardship * 0.5);
            
            if (destination != origin && best_gain > personal_threshold) {
                // Remove from old region
                auto& old_region_index = regionIndex_[origin];
                old_region_index.erase(
                    std::remove(old_region_index.begin(), old_region_index.end(), agent_id),
                    old_region_index.end()
                );
                
                // Add to new region
                agent.region = destination;
                regionIndex_[destination].push_back(agent_id);
                
                // EMERGENT NETWORK RETENTION: Social skill and tie strength determine what survives
                if (agent.neighbors.size() > 2) {
                    // Sociable agents maintain more connections across distance
                    double retention_rate = 0.2 + agent.sociality * 0.4; // 20%-60% based on sociality
                    
                    // Long-distance moves lose more connections
                    double distance_factor = std::abs(static_cast<int>(destination) - static_cast<int>(origin)) 
                                            / static_cast<double>(cfg_.regions);
                    retention_rate *= (1.0 - distance_factor * 0.3);
                    
                    // Conformist agents try harder to maintain existing ties
                    retention_rate += agent.conformity * 0.1;
                    
                    retention_rate = std::clamp(retention_rate, 0.1, 0.8);
                    std::size_t keep_count = static_cast<std::size_t>(agent.neighbors.size() * retention_rate);
                    if (keep_count < 1) keep_count = 1;
                    std::shuffle(agent.neighbors.begin(), agent.neighbors.end(), rng_);
                    agent.neighbors.resize(keep_count);
                }
            }
        }
    }
}

Kernel::Statistics Kernel::getStatistics() const {
    Statistics stats;
    
    // Initialize
    stats.totalAgents = static_cast<std::uint32_t>(agents_.size());
    stats.minAge = cfg_.maxAgeYears;
    stats.maxAge = 0;
    
    // Age sum for average
    std::uint64_t ageSum = 0;
    std::uint64_t connectionSum = 0;
    
    // Belief accumulators
    std::array<double, 4> beliefSum = {0, 0, 0, 0};
    std::vector<double> polarizations;
    polarizations.reserve(agents_.size());
    
    // Regional population counts
    std::vector<std::uint32_t> regionPops(cfg_.regions, 0);
    
    // Process each agent
    for (const auto& agent : agents_) {
        if (!agent.alive) continue;
        
        stats.aliveAgents++;
        
        // Age demographics
        ageSum += agent.age;
        if (agent.age < stats.minAge) stats.minAge = agent.age;
        if (agent.age > stats.maxAge) stats.maxAge = agent.age;
        
        // Age groups
        if (agent.age < 15) stats.children++;
        else if (agent.age < 30) stats.youngAdults++;
        else if (agent.age < 50) stats.middleAge++;
        else if (agent.age < 70) stats.mature++;
        else stats.elderly++;
        
        // Gender
        if (agent.female) stats.females++;
        else stats.males++;
        
        // Network
        connectionSum += agent.neighbors.size();
        if (agent.neighbors.empty()) stats.isolatedAgents++;
        
        // Beliefs
        for (int i = 0; i < 4; ++i) {
            beliefSum[i] += agent.B[i];
        }
        double polarization = std::sqrt(agent.B_norm_sq);
        polarizations.push_back(polarization);
        
        // Region
        regionPops[agent.region]++;
        
        // Language
        stats.langCounts[agent.primaryLang]++;
    }
    
    // Compute averages
    if (stats.aliveAgents > 0) {
        stats.avgAge = static_cast<double>(ageSum) / stats.aliveAgents;
        stats.avgConnections = static_cast<double>(connectionSum) / stats.aliveAgents;
        
        for (int i = 0; i < 4; ++i) {
            stats.avgBeliefs[i] = beliefSum[i] / stats.aliveAgents;
        }
        
        // Polarization statistics
        double polSum = 0.0;
        for (double p : polarizations) {
            polSum += p;
        }
        stats.polarizationMean = polSum / polarizations.size();
        
        double polVarSum = 0.0;
        for (double p : polarizations) {
            double diff = p - stats.polarizationMean;
            polVarSum += diff * diff;
        }
        stats.polarizationStd = std::sqrt(polVarSum / polarizations.size());
    }
    
    // Regional statistics
    std::uint32_t nonEmptyRegions = 0;
    std::uint32_t minPop = cfg_.population;
    std::uint32_t maxPop = 0;
    
    for (auto pop : regionPops) {
        if (pop > 0) {
            nonEmptyRegions++;
            if (pop < minPop) minPop = pop;
            if (pop > maxPop) maxPop = pop;
        }
    }
    
    stats.occupiedRegions = nonEmptyRegions;
    if (nonEmptyRegions > 0) {
        stats.avgPopPerRegion = static_cast<double>(stats.aliveAgents) / nonEmptyRegions;
        stats.minRegionPop = minPop;
        stats.maxRegionPop = maxPop;
    }
    
    // Language count
    for (int i = 0; i < 256; ++i) {
        if (stats.langCounts[i] > 0) {
            stats.numLanguages++;
        }
    }
    
    // Economy metrics (from existing system)
    auto metrics = computeMetrics();
    stats.globalWelfare = metrics.globalWelfare;
    stats.globalInequality = metrics.globalInequality;
    
    // Average income
    double incomeSum = 0.0;
    for (std::size_t i = 0; i < agents_.size(); ++i) {
        if (agents_[i].alive) {
            incomeSum += economy_.getAgentEconomy(i).income;
        }
    }
    if (stats.aliveAgents > 0) {
        stats.avgIncome = incomeSum / stats.aliveAgents;
    }
    
    return stats;
}

