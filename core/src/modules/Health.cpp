#include "modules/Health.h"

#include <algorithm>
#include <random>

#include "kernel/Kernel.h"
#include "modules/Economy.h"

void HealthModule::configure(std::uint32_t regionCount, std::uint64_t seed) {
    regional_snapshots_.assign(regionCount, {});
    rng_.seed(seed);
}

void HealthModule::initializeAgents(std::vector<Agent>& agents) {
    std::uniform_real_distribution<double> noise(-0.05, 0.05);
    for (auto& agent : agents) {
        auto& health = agent.health;
        health.physical_health = clamp01(0.8 + 0.2 * agent.openness - 0.1 * agent.conformity + noise(rng_));
        health.nutrition_level = clamp01(0.8 + noise(rng_));
        health.age_factor = clamp01(0.2 + 0.6 * noise(rng_));
        health.infected = false;
        health.current_disease = nullptr;
        health.immunity = clamp01(0.1 + 0.2 * agent.sociality + noise(rng_));
    }
}

void HealthModule::updateAgents(std::vector<Agent>& agents, const Economy& economy, std::uint64_t /*tick*/) {
    if (regional_snapshots_.empty()) {
        return;
    }

    const std::uint32_t regionCount = static_cast<std::uint32_t>(regional_snapshots_.size());
    for (std::uint32_t r = 0; r < regionCount; ++r) {
        const auto& reg = economy.getRegion(r);
        auto& snapshot = regional_snapshots_[r];
        const double population = std::max(1u, reg.population);
        const double foodPerCapita = reg.production[GoodType::FOOD] / population;
        snapshot.nutrition = clamp01(foodPerCapita);
        snapshot.healthcare = clamp01(reg.welfare * 0.5 + reg.tech_multipliers[GoodType::SERVICES] * 0.5);
        
        // EMERGENT INFECTION PRESSURE: Weights vary by region characteristics
        // Dense urban regions: sanitation matters most
        // Rural regions: healthcare access matters most
        // Poor regions: hardship is dominant factor
        double density = population / 500.0;  // normalized density
        double urbanization = std::min(1.0, density);
        
        // Adaptive weights based on region type
        double hardship_weight = 0.3 + 0.2 * (1.0 - reg.development);  // 0.3-0.5
        double welfare_weight = 0.2 + 0.2 * reg.development;           // 0.2-0.4  
        double efficiency_weight = 0.2 + 0.2 * urbanization;           // 0.2-0.4 (sanitation in cities)
        
        // Normalize weights
        double total_weight = hardship_weight + welfare_weight + efficiency_weight;
        hardship_weight /= total_weight;
        welfare_weight /= total_weight;
        efficiency_weight /= total_weight;
        
        snapshot.infection_pressure = clamp01(
            hardship_weight * reg.hardship + 
            welfare_weight * (1.0 - reg.welfare) + 
            efficiency_weight * (1.0 - reg.efficiency)
        );
        snapshot.avg_health = 0.0;
    }

    if (agents.empty()) {
        return;
    }

    std::vector<std::uint32_t> regionCounts(regionCount, 0);
    std::uniform_real_distribution<double> uni(0.0, 1.0);

    for (auto& agent : agents) {
        auto& health = agent.health;
        auto& snapshot = regional_snapshots_[agent.region];
        regionCounts[agent.region]++;

        health.nutrition_level = 0.7 * health.nutrition_level + 0.3 * snapshot.nutrition;
        const double ageDecay = computeAgeDecay(health.age_factor);
        const double diseaseMortality = (health.infected && health.current_disease) ? health.current_disease->mortality : 0.0;
        const double medicalIntervention = 0.02 + 0.1 * snapshot.healthcare;
        health.physical_health = clamp01(health.physical_health * health.nutrition_level * (1.0 - ageDecay - diseaseMortality) + medicalIntervention);

        // Disease dynamics
        if (!health.infected) {
            const double infectionProb = snapshot.infection_pressure * (1.0 - health.physical_health) * (1.0 - health.immunity);
            if (uni(rng_) < infectionProb) {
                health.infected = true;
                health.current_disease = &baseline_disease_;
            }
        } else {
            const double recoveryProb = baseline_disease_.recovery * (health.physical_health + snapshot.healthcare);
            if (uni(rng_) < recoveryProb) {
                health.infected = false;
                health.immunity = clamp01(health.immunity + baseline_disease_.immunity_boost);
                health.current_disease = nullptr;
            }
        }

        health.immunity = clamp01(health.immunity * 0.995);
        snapshot.avg_health += health.physical_health;
    }

    for (std::uint32_t r = 0; r < regionCount; ++r) {
        if (regionCounts[r] > 0) {
            regional_snapshots_[r].avg_health /= regionCounts[r];
        }
    }
}

double HealthModule::computeAgeDecay(double ageFactor) const {
    const double base = 0.005 + 0.01 * ageFactor;
    return std::clamp(base, 0.0, 0.2);
}

double HealthModule::clamp01(double value) const {
    return std::max(0.0, std::min(1.0, value));
}
