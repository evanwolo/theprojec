#include "kernel/Kernel.h"
#include "kernel/AgentStorage.h"
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
    initAgents();
    buildSmallWorld();
    initEconomicSystems();
    
    // Initialize economy with regions and agents
    economy_.init(cfg_.regions, cfg_.population, rng_, cfg_.startCondition);
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
        
        a.primaryLang = langDist(rng_);
        a.fluency = 0.7 + 0.3 * (uniDist(rng_) - 0.5);
        
        // Personality traits
        a.openness = std::clamp(traitDist(rng_), 0.0, 1.0);
        a.conformity = std::clamp(traitDist(rng_), 0.0, 1.0);
        a.assertiveness = std::clamp(traitDist(rng_), 0.0, 1.0);
        a.sociality = std::clamp(traitDist(rng_), 0.0, 1.0);
        
        // Every 100th agent is a potential charismatic leader
        if (i % 100 == 0) {
            std::uniform_real_distribution<double> charismaticDist(0.8, 0.95);
            a.assertiveness = charismaticDist(rng_);
        }
        
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

#include "kernel/GpuWorker.h"

// ... inside Kernel::step() ...

void Kernel::step() {
    // 1. Sync Legacy -> SoA (Prepare for Physics)
    // This copies the current state of agents into the contiguous arrays
    gpu_storage_.syncFromLegacy(agents_);

    // 2. Run Physics (Dispatch based on compile-time or run-time flag)
    // For now, we assume if CUDA is compiled in, we use it.
#if defined(USE_CUDA)
    launchGpuBeliefUpdate(gpu_storage_, cfg_);
#else
    updateBeliefsSoA();
#endif

    // 3. Sync SoA -> Legacy (Commit changes)
    // This copies the new B0-B3 values back so Economy/Demography can see them
    gpu_storage_.syncToLegacy(agents_);

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
            
            // Economic system shapes ideology (Data-Driven)
            auto it = econ_systems_.find(regional_econ.economic_system);
            if (it != econ_systems_.end()) {
                const auto& mods = it->second.modifiers;
                agent.B[0] += econ_pressure * mods.authority_delta;
                agent.B[1] += econ_pressure * mods.tradition_delta;
                agent.B[2] += econ_pressure * mods.hierarchy_delta;
                agent.B[3] += econ_pressure * mods.religiosity_delta;
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
    
    // Auto-detect movements every 100 ticks
    if (generation_ % 100 == 0) {
        auto clusters = culture_.detectCultures(agents_, 8, 50, 0.5);
        movements_.update(*this, clusters, generation_);
    }
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
    
    // Migration candidates: young adults (age 18-35) with high mobility
    std::vector<std::uint32_t> migration_candidates;
    for (std::size_t i = 0; i < agents_.size(); ++i) {
        const auto& agent = agents_[i];
        if (agent.alive && agent.age >= 18 && agent.age <= 35 && agent.m_mobility > 0.7) {
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
            
            // Migrate if destination is significantly better
            if (destination != origin && best_gain > 0.3) {
                // Remove from old region
                auto& old_region_index = regionIndex_[origin];
                old_region_index.erase(
                    std::remove(old_region_index.begin(), old_region_index.end(), agent_id),
                    old_region_index.end()
                );
                
                // Add to new region
                agent.region = destination;
                regionIndex_[destination].push_back(agent_id);
                
                // Migrants lose some network connections (cultural disruption)
                if (agent.neighbors.size() > 3) {
                    // Keep only 30% of connections
                    std::size_t keep_count = agent.neighbors.size() * 3 / 10;
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

// Helper for the SoA physics
inline double fastTanhSoA(double x) {
    double x2 = x * x;
    return x * (27.0 + x2) / (27.0 + 9.0 * x2);
}

void Kernel::updateBeliefsSoA() {
    // 1. Get the lightweight View
    auto view = gpu_storage_.getView();
    const uint32_t count = view.count;
    const double stepSize = cfg_.stepSize;

    // 2. Buffers for Deltas
    std::vector<double> d0(count, 0.0);
    std::vector<double> d1(count, 0.0);
    std::vector<double> d2(count, 0.0);
    std::vector<double> d3(count, 0.0);

    // 3. Parallel Physics Loop
    #pragma omp parallel for schedule(dynamic)
    for (uint32_t i = 0; i < count; ++i) {
        double myB0 = view.B0[i];
        double myB1 = view.B1[i];
        double myB2 = view.B2[i];
        double myB3 = view.B3[i];
        double mySusc = view.susceptibility[i];
        double myFluency = view.fluency[i];
        double myNormSq = myB0*myB0 + myB1*myB1 + myB2*myB2 + myB3*myB3;

        int start = view.neighbor_offsets[i];
        int end   = start + view.neighbor_counts[i];

        double delta0=0, delta1=0, delta2=0, delta3=0;

        for (int idx = start; idx < end; ++idx) {
            int nbrId = view.neighbor_indices[idx];
            
            double tB0 = view.B0[nbrId];
            double tB1 = view.B1[nbrId];
            double tB2 = view.B2[nbrId];
            double tB3 = view.B3[nbrId];
            
            double dot = myB0*tB0 + myB1*tB1 + myB2*tB2 + myB3*tB3;
            double tNormSq = tB0*tB0 + tB1*tB1 + tB2*tB2 + tB3*tB3;
            
            double sim = 1.0;
            if (myNormSq * tNormSq > 1e-9) sim = dot / std::sqrt(myNormSq * tNormSq);
            
            double gate = (sim - cfg_.simFloor) / (1.0 - cfg_.simFloor);
            if (gate <= 0.0) continue;

            double langQ = 0.5 * (myFluency + view.fluency[nbrId]);
            double weight = stepSize * gate * langQ * mySusc;

            delta0 += weight * fastTanhSoA(tB0 - myB0);
            delta1 += weight * fastTanhSoA(tB1 - myB1);
            delta2 += weight * fastTanhSoA(tB2 - myB2);
            delta3 += weight * fastTanhSoA(tB3 - myB3);
        }
        d0[i] = delta0; d1[i] = delta1; d2[i] = delta2; d3[i] = delta3;
    }

    // 4. Apply Deltas
    #pragma omp parallel for
    for (uint32_t i = 0; i < count; ++i) {
        view.B0[i] = std::max(-1.0, std::min(1.0, view.B0[i] + d0[i]));
        view.B1[i] = std::max(-1.0, std::min(1.0, view.B1[i] + d1[i]));
        view.B2[i] = std::max(-1.0, std::min(1.0, view.B2[i] + d2[i]));
        view.B3[i] = std::max(-1.0, std::min(1.0, view.B3[i] + d3[i]));
    }
}

void Kernel::initEconomicSystems() {
    econ_systems_.clear();

    // Market: Favor Liberty (-B0), Accept Hierarchy (+B2)
    econ_systems_["market"] = {
        "market",
        { -0.3, 0.0, 0.2, 0.0 } // authority_delta, tradition_delta, hierarchy_delta, religiosity_delta
    };

    // Planned: Favor Authority (+B0), Demand Equality (-B2)
    econ_systems_["planned"] = {
        "planned",
        { 0.3, 0.0, -0.3, 0.0 }
    };

    // Feudal: Tradition (+B1), Hierarchy (+B2)
    econ_systems_["feudal"] = {
        "feudal",
        { 0.0, 0.4, 0.4, 0.0 }
    };

    // Cooperative: Equality (-B2), Liberty (-B0)
    econ_systems_["cooperative"] = {
        "cooperative",
        { -0.2, 0.0, -0.3, 0.0 }
    };
}

