#ifndef HEALTH_MODULE_H
#define HEALTH_MODULE_H

#include <cstdint>
#include <random>
#include <vector>

struct Agent;
class Economy;

struct Disease {
    double infectivity = 0.25;
    double mortality = 0.03;
    double recovery = 0.04;
    double immunity_boost = 0.2;
};

struct HealthState {
    double physical_health = 1.0;
    bool infected = false;
    const Disease* current_disease = nullptr;
    double nutrition_level = 1.0;
    double age_factor = 0.0;
    double immunity = 0.0;
};

struct RegionalHealthSnapshot {
    double nutrition = 1.0;
    double healthcare = 0.5;
    double infection_pressure = 0.0;
    double avg_health = 1.0;
};

class HealthModule {
public:
    void configure(std::uint32_t regionCount, std::uint64_t seed);
    void initializeAgents(std::vector<Agent>& agents);
    void updateAgents(std::vector<Agent>& agents, const Economy& economy, std::uint64_t tick);

    const std::vector<RegionalHealthSnapshot>& regionalSnapshots() const { return regional_snapshots_; }

private:
    std::vector<RegionalHealthSnapshot> regional_snapshots_;
    std::mt19937_64 rng_{};
    Disease baseline_disease_{};

    double computeAgeDecay(double ageFactor) const;
    double clamp01(double value) const;
};

#endif
