#include "modules/Psychology.h"

#include <algorithm>
#include <cmath>
#include <random>

#include "kernel/Kernel.h"
#include "modules/Economy.h"

namespace {
constexpr double kStressShockFloor = 0.05;
constexpr double kStressShockCeil = 1.5;

// EMERGENT STRESS SENSITIVITY: Personality determines how different stressors affect individuals
struct StressSensitivity {
    double economic;      // sensitivity to economic hardship
    double media;         // sensitivity to negative information
    double institutional; // sensitivity to institutional failures
    double disease;       // sensitivity to health threats
};

StressSensitivity computeStressSensitivity(const Agent& agent) {
    StressSensitivity sens;
    
    // Economic sensitivity: high for materialistic (low openness), low for adaptable (high openness)
    sens.economic = 0.4 + 0.4 * (1.0 - agent.openness) + 0.2 * agent.conformity;
    
    // Media sensitivity: conformists are more affected by media narratives
    sens.media = 0.2 + 0.5 * agent.conformity - 0.2 * agent.assertiveness;
    
    // Institutional sensitivity: non-conformists frustrated by rigid institutions
    sens.institutional = 0.3 + 0.4 * (1.0 - agent.conformity) + 0.2 * agent.assertiveness;
    
    // Disease sensitivity: varies with sociality (social people more anxious about disease spread)
    sens.disease = 0.2 + 0.3 * agent.sociality + 0.2 * (1.0 - agent.openness);
    
    return sens;
}
}

void PsychologyModule::configure(std::uint32_t regionCount, std::uint64_t seed) {
    regional_profiles_.assign(regionCount, {});
    regional_metrics_.assign(regionCount, {});
    rng_.seed(seed);
}

void PsychologyModule::initializeAgents(std::vector<Agent>& agents) {
    std::uniform_real_distribution<double> noise(-0.05, 0.05);
    for (auto& agent : agents) {
        auto& psych = agent.psych;
        psych.resilience = clamp01(0.35 + 0.25 * agent.conformity + 0.2 * agent.sociality + 0.1 * agent.openness + noise(rng_));
        psych.mental_health = clamp01(psych.resilience + 0.2 * (agent.sociality - 0.5) + noise(rng_));
        psych.stress_level = clamp01(0.2 + 0.1 * (1.0 - psych.resilience) + noise(rng_));
        psych.cognitive_bias = clamp01(1.0 + 0.2 * (agent.assertiveness - agent.conformity));
        psych.stressors.fill(0.0);
        psych.recovery_memory = 0.0;
        psych.last_shock_tick = 0;
    }
}

void PsychologyModule::updateAgents(std::vector<Agent>& agents, const Economy& economy, std::uint64_t tick) {
    if (regional_profiles_.empty()) {
        return;
    }

    const std::uint32_t regionCount = static_cast<std::uint32_t>(regional_profiles_.size());
    for (std::uint32_t r = 0; r < regionCount; ++r) {
        const auto& reg = economy.getRegion(r);
        auto& profile = regional_profiles_[r];
        profile.hardship = clamp01(reg.hardship);
        profile.inequality = clamp01(reg.inequality);
        profile.welfare = clamp01(reg.welfare);
        profile.institutional_support = clamp01(reg.efficiency);
        profile.media_negativity = clamp01(1.0 - reg.system_stability);

        regional_metrics_[r].avg_stress = 0.0;
        regional_metrics_[r].avg_mental_health = 0.0;
        regional_metrics_[r].low_mental_health_share = 0.0;
    }

    if (agents.empty()) {
        return;
    }

    const auto& econAgents = economy.agents();
    for (auto& agent : agents) {
        auto& psych = agent.psych;
        const auto& econRegion = regional_profiles_[agent.region];
        const auto& agentEcon = econAgents[agent.id];

        // EMERGENT STRESS: Sensitivity varies by personality
        StressSensitivity sens = computeStressSensitivity(agent);
        
        const double economicShock = sens.economic * (0.6 * agentEcon.hardship + 0.4 * econRegion.hardship);
        const double mediaShock = sens.media * econRegion.media_negativity;
        const double institutionalShock = sens.institutional * (1.0 - econRegion.institutional_support);
        const double diseaseShock = sens.disease * agent.health.infected;

        psych.stressors[toIndex(StressSource::EconomicHardship)] = economicShock;
        psych.stressors[toIndex(StressSource::MediaNegativity)] = mediaShock;
        psych.stressors[toIndex(StressSource::InstitutionalRigidity)] = institutionalShock;
        psych.stressors[toIndex(StressSource::DiseaseImpact)] = diseaseShock;
        psych.stressors[toIndex(StressSource::WarPressure)] = 0.0; // placeholder until war module integration

        double totalShock = economicShock + mediaShock + institutionalShock + diseaseShock;
        totalShock = std::clamp(totalShock, kStressShockFloor, kStressShockCeil);
        totalShock *= (1.0 - psych.resilience);

        const double socialSupport = clamp01(0.5 + 0.5 * (1.0 - econRegion.inequality));
        const double recoveryRate = 0.05 + 0.3 * econRegion.welfare + 0.2 * socialSupport;
        const double decay = psych.stress_level * psych.stress_level * (1.0 - socialSupport);

        psych.stress_level = clamp01(psych.stress_level + totalShock - recoveryRate * (0.5 + psych.mental_health));
        psych.mental_health = clamp01(psych.mental_health * (1.0 - decay) + psych.resilience * (econRegion.welfare + socialSupport) * 0.25);
        psych.cognitive_bias = std::clamp(1.0 + 0.5 * (psych.stress_level - 0.5) + 0.3 * (agent.assertiveness - agent.conformity), 0.25, 2.0);

        const double comm = clamp01(1.0 - 0.4 * psych.stress_level + 0.3 * psych.mental_health);
        const double mobility = clamp01(0.8 + 0.4 * agent.sociality + 0.3 * (psych.mental_health - 0.5) - 0.2 * psych.stress_level);
        agent.m_comm = comm;
        agent.m_mobility = std::clamp(mobility, 0.1, 1.5);

        auto& metrics = regional_metrics_[agent.region];
        metrics.avg_stress += psych.stress_level;
        metrics.avg_mental_health += psych.mental_health;
        if (psych.mental_health < 0.3) {
            metrics.low_mental_health_share += 1.0;
        }
    }

    // Normalize metrics
    std::vector<std::uint32_t> regionCounts(regional_profiles_.size(), 0);
    for (const auto& agent : agents) {
        regionCounts[agent.region]++;
    }
    for (std::uint32_t r = 0; r < regionCount; ++r) {
        const double inv = regionCounts[r] > 0 ? 1.0 / regionCounts[r] : 0.0;
        regional_metrics_[r].avg_stress *= inv;
        regional_metrics_[r].avg_mental_health *= inv;
        regional_metrics_[r].low_mental_health_share *= inv;
    }
}

double PsychologyModule::clamp01(double value) const {
    return std::max(0.0, std::min(1.0, value));
}
