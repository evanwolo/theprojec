#ifndef PSYCHOLOGY_MODULE_H
#define PSYCHOLOGY_MODULE_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

struct Agent;
class Economy;

enum class StressSource : std::uint8_t {
    EconomicHardship = 0,
    WarPressure = 1,
    MediaNegativity = 2,
    InstitutionalRigidity = 3,
    DiseaseImpact = 4,
    COUNT
};

struct PsychologicalState {
    double stress_level = 0.0;
    double resilience = 0.5;
    double mental_health = 0.5;
    double cognitive_bias = 1.0;
    std::array<double, static_cast<std::size_t>(StressSource::COUNT)> stressors{};
    double recovery_memory = 0.0;
    std::uint64_t last_shock_tick = 0;
};

struct RegionalStressProfile {
    double hardship = 0.0;
    double inequality = 0.0;
    double welfare = 1.0;
    double institutional_support = 1.0;
    double media_negativity = 0.0;
};

struct RegionalPsychologyMetrics {
    double avg_stress = 0.0;
    double avg_mental_health = 0.0;
    double low_mental_health_share = 0.0;
};

class PsychologyModule {
public:
    void configure(std::uint32_t regionCount, std::uint64_t seed);
    void initializeAgents(std::vector<Agent>& agents);
    void updateAgents(std::vector<Agent>& agents, const Economy& economy, std::uint64_t tick);

    const std::vector<RegionalPsychologyMetrics>& regionalMetrics() const { return regional_metrics_; }

private:
    std::vector<RegionalStressProfile> regional_profiles_;
    std::vector<RegionalPsychologyMetrics> regional_metrics_;
    std::mt19937_64 rng_{};

    double clamp01(double value) const;
    static std::size_t toIndex(StressSource src) { return static_cast<std::size_t>(src); }
};

#endif
