#include "modules/CohortDemographics.h"
#include "kernel/Kernel.h"
#include <algorithm>
#include <cmath>

void CohortDemographics::configure(std::uint32_t num_regions, std::uint64_t seed) {
    num_regions_ = num_regions;
    rng_state_ = seed;
    cohorts_.clear();
    cohorts_.reserve(num_regions * 18 * 2);  // regions × age_groups × genders
}

std::uint8_t CohortDemographics::ageToGroup(int age) const {
    // 5-year buckets: [0-4], [5-9], ..., [85-89], [90+]
    if (age >= 90) return 17;
    return static_cast<std::uint8_t>(age / 5);
}

void CohortDemographics::buildCohortsFromAgents(const std::vector<Agent>& agents) {
    cohorts_.clear();
    
    // Aggregate agents into cohorts
    for (const auto& agent : agents) {
        if (!agent.alive) continue;
        
        CohortKey key{
            static_cast<std::uint16_t>(agent.region),
            ageToGroup(agent.age),
            static_cast<std::uint8_t>(agent.female)
        };
        
        auto& cohort = cohorts_[key];
        cohort.count++;
        cohort.avg_health += agent.health.physical_health;
        cohort.avg_nutrition += agent.health.nutrition_level;
        if (agent.health.immunity > 0.3) cohort.immunity_share += 1.0;
        if (agent.health.infected) cohort.infected_share += 1.0;
    }
    
    // Normalize aggregates
    for (auto& [key, cohort] : cohorts_) {
        if (cohort.count > 0) {
            const double inv = 1.0 / cohort.count;
            cohort.avg_health *= inv;
            cohort.avg_nutrition *= inv;
            cohort.immunity_share *= inv;
            cohort.infected_share *= inv;
        }
        
        // Compute demographic rates
        cohort.mortality_rate = computeMortalityRate(key.age_group);
        cohort.fertility_rate = (key.female == 1) ? computeFertilityRate(key.age_group) : 0.0;
    }
}

double CohortDemographics::computeMortalityRate(std::uint8_t age_group) const {
    // EMERGENT MORTALITY: Base rate modified by regional factors during updateDemographics()
    // This provides age-structured baseline; actual mortality varies by region health/welfare
    constexpr double TICK_SCALE = 0.1;  // 10 ticks/year
    
    // Base mortality curve (will be modified by regional factors)
    if (age_group == 0) return 0.008 * TICK_SCALE;      // 0-4: infant mortality (varies most by region)
    if (age_group <= 2) return 0.002 * TICK_SCALE;       // 5-14: low base
    if (age_group <= 9) return 0.003 * TICK_SCALE;       // 15-49: prime years
    if (age_group <= 13) return 0.008 * TICK_SCALE;      // 50-69: rising
    if (age_group <= 15) return 0.025 * TICK_SCALE;      // 70-79: significant
    if (age_group <= 16) return 0.060 * TICK_SCALE;      // 80-89: high
    return 0.150 * TICK_SCALE;                            // 90+: very high
}

double CohortDemographics::computeFertilityRate(std::uint8_t age_group) const {
    // EMERGENT FERTILITY: Base rate that varies by regional development/culture
    // Developed regions: lower base fertility, later peak age
    // Traditional regions: higher fertility, earlier peak
    constexpr double TICK_SCALE = 0.1;  // 10 ticks/year
    
    // Base TFR varies by development (set during cohort sync with economy)
    // This is just the age distribution curve
    constexpr double BASE_TFR = 2.5;    // Will be modified per-region
    constexpr double FERTILE_YEARS = 30.0;
    constexpr double BIRTH_RATE = (BASE_TFR / FERTILE_YEARS) * TICK_SCALE;
    
    if (age_group < 3 || age_group > 8) return 0.0;
    
    // Age-specific fertility rates (peak varies by region development)
    if (age_group == 5) return BIRTH_RATE * 1.2;  // 25-30: peak
    if (age_group == 4 || age_group == 6) return BIRTH_RATE * 1.0;
    if (age_group == 3 || age_group == 7) return BIRTH_RATE * 0.7;
    if (age_group == 8) return BIRTH_RATE * 0.3;
    
    return 0.0;
}

std::uint32_t CohortDemographics::randomBinomial(std::uint32_t n, double p) {
    // Simple LCG: X_{n+1} = (a * X_n + c) mod m
    constexpr std::uint64_t A = 6364136223846793005ULL;
    constexpr std::uint64_t C = 1442695040888963407ULL;
    
    if (n == 0 || p <= 0.0) return 0;
    if (p >= 1.0) return n;
    
    // For large n, use normal approximation
    if (n > 100) {
        double mean = n * p;
        double stddev = std::sqrt(n * p * (1.0 - p));
        
        // Box-Muller transform for normal random
        rng_state_ = A * rng_state_ + C;
        double u1 = (rng_state_ >> 11) * (1.0 / 9007199254740992.0);
        rng_state_ = A * rng_state_ + C;
        double u2 = (rng_state_ >> 11) * (1.0 / 9007199254740992.0);
        
        double z = std::sqrt(-2.0 * std::log(u1)) * std::cos(2.0 * 3.14159265358979323846 * u2);
        double result = mean + stddev * z;
        return static_cast<std::uint32_t>(std::max(0.0, std::min(static_cast<double>(n), result)));
    }
    
    // For small n, use direct simulation
    std::uint32_t count = 0;
    for (std::uint32_t i = 0; i < n; ++i) {
        rng_state_ = A * rng_state_ + C;
        double u = (rng_state_ >> 11) * (1.0 / 9007199254740992.0);
        if (u < p) count++;
    }
    return count;
}

void CohortDemographics::updateDemographics(std::uint64_t tick, int ticks_per_year) {
    // Process deaths
    for (auto& [key, cohort] : cohorts_) {
        if (cohort.count == 0) continue;
        
        // EMERGENT MORTALITY: Base rate modified by multiple regional factors
        double health_factor = 0.5 + 0.5 * cohort.avg_health;  // [0.5, 1.0]
        double effective_mortality = cohort.mortality_rate / health_factor;
        
        // Regional development reduces mortality (better healthcare, sanitation)
        // Infant mortality (age_group 0) is most affected by development
        double development_modifier = 1.0 - (cohort.avg_nutrition * 0.3);  // nutrition proxy for development
        if (key.age_group == 0) {
            development_modifier = 1.0 - (cohort.avg_nutrition * 0.6);  // infants benefit most
        }
        effective_mortality *= std::max(0.3, development_modifier);
        
        // Infection increases mortality
        if (cohort.infected_share > 0.0) {
            effective_mortality *= (1.0 + 0.3 * cohort.infected_share);
        }
        
        std::uint32_t deaths = randomBinomial(cohort.count, effective_mortality);
        cohort.count = (deaths < cohort.count) ? (cohort.count - deaths) : 0;
    }
    
    // Process births (from fertile female cohorts)
    std::vector<std::pair<CohortKey, std::uint32_t>> births;
    
    for (const auto& [key, cohort] : cohorts_) {
        if (cohort.count == 0 || cohort.fertility_rate <= 0.0) continue;
        
        // EMERGENT FERTILITY: Multiple factors affect birth rates
        double nutrition_factor = 0.5 + 0.5 * cohort.avg_nutrition;
        
        // Demographic transition: higher development = lower fertility
        // Well-nourished populations with high health have fewer children (quality over quantity)
        double demographic_transition = 1.0 - (cohort.avg_health * 0.3);  // health proxy for development
        
        // Economic stress affects fertility (hardship reduces family planning)
        // But extreme hardship also reduces fertility (survival mode)
        double stress_factor = 1.0;
        // (Note: would need access to regional hardship here for full implementation)
        
        double effective_fertility = cohort.fertility_rate * nutrition_factor * demographic_transition * stress_factor;
        
        // Age-group specific modifiers for peak shift
        // In developed regions, fertility peaks later; in traditional regions, peaks earlier
        // This is approximated by health/nutrition levels
        if (key.age_group == 3 && cohort.avg_health > 0.7) {
            effective_fertility *= 0.6;  // developed: delay early fertility
        }
        if (key.age_group == 6 && cohort.avg_health > 0.7) {
            effective_fertility *= 1.2;  // developed: more late fertility
        }
        
        std::uint32_t num_births = randomBinomial(cohort.count, effective_fertility);
        
        if (num_births > 0) {
            // Create newborn cohorts (age group 0, 50/50 gender split)
            std::uint32_t male_births = num_births / 2;
            std::uint32_t female_births = num_births - male_births;
            
            if (male_births > 0) {
                births.emplace_back(CohortKey{key.region, 0, 0}, male_births);
            }
            if (female_births > 0) {
                births.emplace_back(CohortKey{key.region, 0, 1}, female_births);
            }
        }
    }
    
    // Add births to cohorts
    for (const auto& [birth_key, count] : births) {
        auto& cohort = cohorts_[birth_key];
        cohort.count += count;
        // Newborns start with baseline health
        cohort.avg_health = 0.9;
        cohort.avg_nutrition = 0.8;
        cohort.mortality_rate = computeMortalityRate(birth_key.age_group);
        cohort.fertility_rate = computeFertilityRate(birth_key.age_group);
    }
    
    // Process aging (every year)
    if (tick % ticks_per_year == 0) {
        std::unordered_map<CohortKey, Cohort, CohortKeyHash> aged_cohorts;
        aged_cohorts.reserve(cohorts_.size());
        
        for (auto& [key, cohort] : cohorts_) {
            if (cohort.count == 0) continue;
            
            // Age cohort up
            CohortKey new_key = key;
            new_key.age_group = std::min(static_cast<std::uint8_t>(key.age_group + 1), 
                                          static_cast<std::uint8_t>(17));  // Max age group
            
            // Move to new age group
            auto& target = aged_cohorts[new_key];
            if (target.count == 0) {
                target = cohort;
                target.mortality_rate = computeMortalityRate(new_key.age_group);
                target.fertility_rate = computeFertilityRate(new_key.age_group);
            } else {
                // Merge cohorts (weighted average)
                double total = target.count + cohort.count;
                target.avg_health = (target.avg_health * target.count + cohort.avg_health * cohort.count) / total;
                target.avg_nutrition = (target.avg_nutrition * target.count + cohort.avg_nutrition * cohort.count) / total;
                target.immunity_share = (target.immunity_share * target.count + cohort.immunity_share * cohort.count) / total;
                target.infected_share = (target.infected_share * target.count + cohort.infected_share * cohort.count) / total;
                target.count += cohort.count;
            }
        }
        
        cohorts_ = std::move(aged_cohorts);
    }
}

void CohortDemographics::updateHealth(const std::vector<double>& regional_nutrition,
                                      const std::vector<double>& regional_healthcare,
                                      const std::vector<double>& regional_infection_pressure) {
    for (auto& [key, cohort] : cohorts_) {
        if (cohort.count == 0) continue;
        if (key.region >= regional_nutrition.size()) continue;
        
        // Update nutrition (smooth convergence)
        double target_nutrition = regional_nutrition[key.region];
        cohort.avg_nutrition = 0.8 * cohort.avg_nutrition + 0.2 * target_nutrition;
        
        // Update health (affected by nutrition, age, healthcare)
        double healthcare = regional_healthcare[key.region];
        double age_decay = computeMortalityRate(key.age_group) * 5.0;  // Scaled decay
        
        double health_change = 0.1 * (cohort.avg_nutrition - 0.5) - age_decay + 0.05 * healthcare;
        cohort.avg_health = clamp01(cohort.avg_health + health_change);
        
        // Disease dynamics (simplified)
        double infection_pressure = regional_infection_pressure[key.region];
        double infection_prob = infection_pressure * (1.0 - cohort.avg_health) * (1.0 - cohort.immunity_share);
        double new_infections = infection_prob * 0.1;  // Rate per tick
        
        double recovery_prob = 0.04 * (cohort.avg_health + healthcare);
        double recoveries = cohort.infected_share * recovery_prob;
        
        cohort.infected_share = clamp01(cohort.infected_share + new_infections - recoveries);
        cohort.immunity_share = clamp01(cohort.immunity_share + recoveries * 0.2);
        
        // Immunity decays slowly
        cohort.immunity_share *= 0.999;
    }
}

void CohortDemographics::syncToAgents(std::vector<Agent>& agents, std::uint64_t tick) {
    // Build agent-to-cohort mapping
    std::unordered_map<CohortKey, std::vector<std::uint32_t>, CohortKeyHash> agent_lists;
    
    for (auto& agent : agents) {
        if (!agent.alive) continue;
        
        CohortKey key{
            static_cast<std::uint16_t>(agent.region),
            ageToGroup(agent.age),
            static_cast<std::uint8_t>(agent.female)
        };
        
        agent_lists[key].push_back(agent.id);
    }
    
    // Sync cohort data to agents
    for (const auto& [key, cohort] : cohorts_) {
        auto it = agent_lists.find(key);
        if (it == agent_lists.end()) continue;
        
        const auto& agent_ids = it->second;
        std::uint32_t cohort_size = cohort.count;
        std::uint32_t agent_list_size = static_cast<std::uint32_t>(agent_ids.size());
        
        // If cohort shrank, mark excess agents as dead
        if (cohort_size < agent_list_size) {
            std::uint32_t to_kill = agent_list_size - cohort_size;
            for (std::uint32_t i = 0; i < to_kill && i < agent_ids.size(); ++i) {
                agents[agent_ids[i]].alive = false;
            }
        }
        
        // Sync health data to surviving agents
        for (std::uint32_t i = 0; i < std::min(cohort_size, agent_list_size); ++i) {
            auto& agent = agents[agent_ids[i]];
            agent.health.physical_health = cohort.avg_health;
            agent.health.nutrition_level = cohort.avg_nutrition;
            agent.health.infected = (i < static_cast<std::uint32_t>(cohort.infected_share * cohort_size));
            agent.health.immunity = (cohort.immunity_share > 0.5) ? 0.5 : 0.2;
        }
    }
    
    // Handle births: we don't create new agents here, that's handled separately
    // This sync only updates existing agents with cohort statistics
}

std::uint32_t CohortDemographics::getTotalPopulation() const {
    std::uint32_t total = 0;
    for (const auto& [key, cohort] : cohorts_) {
        total += cohort.count;
    }
    return total;
}

std::uint32_t CohortDemographics::getRegionPopulation(std::uint32_t region) const {
    std::uint32_t total = 0;
    for (const auto& [key, cohort] : cohorts_) {
        if (key.region == region) {
            total += cohort.count;
        }
    }
    return total;
}

double CohortDemographics::getRegionAvgHealth(std::uint32_t region) const {
    double total_health = 0.0;
    std::uint32_t total_count = 0;
    
    for (const auto& [key, cohort] : cohorts_) {
        if (key.region == region) {
            total_health += cohort.avg_health * cohort.count;
            total_count += cohort.count;
        }
    }
    
    return (total_count > 0) ? (total_health / total_count) : 0.8;
}

double CohortDemographics::clamp01(double value) const {
    return std::max(0.0, std::min(1.0, value));
}
