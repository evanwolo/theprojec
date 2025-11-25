#ifndef COHORT_DEMOGRAPHICS_H
#define COHORT_DEMOGRAPHICS_H

#include <array>
#include <vector>
#include <cstdint>
#include <unordered_map>

struct Agent;

// Cohort key: [Region, AgeGroup, Gender]
struct CohortKey {
    std::uint16_t region;
    std::uint8_t age_group;  // 0-17 (5-year buckets: 0-4, 5-9, ..., 85-89)
    std::uint8_t female;     // 0=male, 1=female
    
    bool operator==(const CohortKey& other) const {
        return region == other.region && age_group == other.age_group && female == other.female;
    }
};

// Hash function for CohortKey
struct CohortKeyHash {
    std::size_t operator()(const CohortKey& k) const {
        // Combine into single 32-bit value: [region(16)] [age_group(8)] [female(8)]
        std::uint32_t combined = (static_cast<std::uint32_t>(k.region) << 16) |
                                  (static_cast<std::uint32_t>(k.age_group) << 8) |
                                  static_cast<std::uint32_t>(k.female);
        // Simple hash
        combined ^= (combined >> 16);
        combined *= 0x85ebca6b;
        combined ^= (combined >> 13);
        combined *= 0xc2b2ae35;
        combined ^= (combined >> 16);
        return static_cast<std::size_t>(combined);
    }
};

// Cohort data (aggregate statistics)
struct Cohort {
    std::uint32_t count = 0;                // Number of agents in this cohort
    double avg_health = 0.8;                // Average physical health
    double avg_nutrition = 0.8;             // Average nutrition level
    double immunity_share = 0.0;            // Proportion with immunity
    double infected_share = 0.0;            // Proportion currently infected
    
    // Mortality and fertility rates (computed from age group)
    double mortality_rate = 0.0;            // Per-tick death probability
    double fertility_rate = 0.0;            // Per-tick birth probability (females only)
};

class CohortDemographics {
public:
    void configure(std::uint32_t num_regions, std::uint64_t seed);
    
    // Convert agents to cohorts
    void buildCohortsFromAgents(const std::vector<Agent>& agents);
    
    // Update cohort demographics (births, deaths, aging)
    void updateDemographics(std::uint64_t tick, int ticks_per_year);
    
    // Update health dynamics at cohort level
    void updateHealth(const std::vector<double>& regional_nutrition,
                      const std::vector<double>& regional_healthcare,
                      const std::vector<double>& regional_infection_pressure);
    
    // Apply cohort changes back to agent population
    void syncToAgents(std::vector<Agent>& agents, std::uint64_t tick);
    
    // Query
    std::uint32_t getTotalPopulation() const;
    std::uint32_t getRegionPopulation(std::uint32_t region) const;
    double getRegionAvgHealth(std::uint32_t region) const;
    
    const std::unordered_map<CohortKey, Cohort, CohortKeyHash>& cohorts() const { 
        return cohorts_; 
    }

private:
    std::unordered_map<CohortKey, Cohort, CohortKeyHash> cohorts_;
    std::uint32_t num_regions_ = 0;
    std::uint64_t rng_state_;  // Simple LCG for deterministic randomness
    
    // Helper functions
    std::uint8_t ageToGroup(int age) const;
    double computeMortalityRate(std::uint8_t age_group) const;
    double computeFertilityRate(std::uint8_t age_group) const;
    std::uint32_t randomBinomial(std::uint32_t n, double p);
    double clamp01(double value) const;
};

#endif
