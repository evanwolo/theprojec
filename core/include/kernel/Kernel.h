#ifndef KERNEL_H
#define KERNEL_H

#include <array>
#include <vector>
#include <cstdint>
#include <string>
#include <random>
#include "modules/Economy.h"
#include "modules/Psychology.h"
#include "modules/Health.h"
#include "modules/MeanField.h"
#include "utils/EventLog.h"

// ---------- Tuning Constants ----------
// These constants control emergent behavior dynamics and have been empirically tuned.
// Changing these affects simulation outcomes - document rationale for any changes.
namespace TuningConstants {
    // Belief dynamics
    constexpr double kHomophilyExponent = 2.5;       // Exponential homophily strength (exp(sim * this))
    constexpr double kHomophilyMinWeight = 0.1;     // Minimum neighbor influence weight
    constexpr double kHomophilyMaxWeight = 10.0;    // Maximum neighbor influence weight
    constexpr double kLanguageBonusMultiplier = 1.5; // Shared language influence bonus
    constexpr double kInnovationNoise = 0.03;       // Belief innovation std dev
    
    // Belief anchoring (age-based resistance to change)
    constexpr double kAnchoringMaxAge = 50.0;       // Age at which anchoring maxes out
    constexpr double kAnchoringBase = 0.3;          // Minimum anchoring (young agents)
    constexpr double kAnchoringAgeWeight = 0.4;     // Age contribution to anchoring
    constexpr double kAnchoringAssertWeight = 0.2;  // Assertiveness contribution to anchoring
    
    // Network dynamics
    constexpr int kReconnectInterval = 5;           // Ticks between reconnection passes
    constexpr double kReconnectCapFraction = 0.02;  // Max fraction of agents reconnected per tick
    constexpr double kNeighborWeightMin = 0.5;      // Minimum neighbor influence weight (vs regional)
    constexpr double kNeighborWeightMax = 0.85;     // Maximum neighbor influence weight (vs regional)
    
    // Demography
    constexpr double kAgeShiftBase = 0.6;           // Base age shift for belief inheritance
    constexpr double kAgeShiftMaxBonus = 0.4;       // Max bonus from age
    constexpr double kAgeShiftNormalizer = 25.0;    // Age normalization factor
    
    // Migration
    constexpr double kHardshipPushWeight = 2.0;     // Hardship contribution to push factor
    constexpr double kCrowdingPenaltyWeight = 0.5;  // Over-capacity crowding penalty
    
    // Economic experience → belief pressure
    constexpr double kBasePressureMultiplier = 0.05; // Base pressure from economic experience
    constexpr double kHardshipThreshold = 0.3;       // Hardship level that triggers belief response
    constexpr double kWelfareThreshold = 0.5;        // Welfare level that triggers openness response
}

// ---------- Configuration ----------
struct KernelConfig {
    std::uint32_t population = 50000;
    std::uint32_t regions = 200;
    std::uint32_t avgConnections = 8;  // k (even)
    double rewireProb = 0.05;           // p for Watts-Strogatz
    double stepSize = 0.15;             // eta (global influence rate)
    double simFloor = 0.05;             // minimum similarity gate
    bool useMeanField = true;           // Use mean field approximation (faster)
    std::uint64_t seed = 42;
    std::string startCondition = "baseline"; // economic starting profile
    
    // Demography
    int ticksPerYear = 10;              // age granularity
    int maxAgeYears = 90;               // hard cap on lifespan
    double regionCapacity = 500.0;      // target population per region
    bool demographyEnabled = true;      // enable births/deaths
};

// ---------- Agent Structure ----------
struct Agent {
    // Identity
    std::uint32_t id = 0;
    std::uint32_t region = 0;
    bool alive = true;
    
    // Demography
    int age = 0;                        // in years (tick-based)
    bool female = false;
    
    // Lineage (Phase 2 integration point)
    std::int32_t parent_a = -1;
    std::int32_t parent_b = -1;
    std::uint32_t lineage_id = 0;
    
    // Language: family (0-3) + dialect (0-255 for regional variation)
    // Language families represent major language groups
    // Dialects encode regional variation within a family
    std::uint8_t primaryLang = 0;    // language family (0-3)
    std::uint8_t dialect = 0;         // regional dialect within family
    double fluency = 1.0;             // 0..1
    
    // Personality traits (0..1, mean ~0.5)
    double openness = 0.5;
    double conformity = 0.5;
    double assertiveness = 0.5;
    double sociality = 0.5;
    
    // Belief state: internal x (unbounded), observable B = tanh(x)
    std::array<double, 4> x{0, 0, 0, 0};  // internal state
    std::array<double, 4> B{0, 0, 0, 0};  // beliefs [-1,1]
    double B_norm_sq = 0.0; // cached squared norm of B
    
    // Module multipliers (written by tech/media/economy modules)
    double m_comm = 1.0;        // communication reach/speed
    double m_susceptibility = 1.0;  // influence susceptibility
    double m_mobility = 1.0;    // migration/relocation ease
    
    PsychologicalState psych;
    HealthState health;

    // Network (sparse adjacency)
    std::vector<std::uint32_t> neighbors;
};

// ---------- Kernel Engine ----------
class Kernel {
public:
    explicit Kernel(const KernelConfig& cfg);
    
    // Lifecycle
    void reset(const KernelConfig& cfg);
    void step();
    void stepN(int n);
    
    // Access
    const std::vector<Agent>& agents() const { return agents_; }
    std::vector<Agent>& agentsMut() { return agents_; }
    const std::vector<std::vector<std::uint32_t>>& regionIndex() const { return regionIndex_; }
    std::uint64_t generation() const { return generation_; }
    
    // Economy access
    const Economy& economy() const { return economy_; }
    Economy& economyMut() { return economy_; }
    
    // Event log access
    EventLog& eventLog() { return event_log_; }
    const EventLog& eventLog() const { return event_log_; }
    
    // Metrics (lightweight for logging)
    struct Metrics {
        double polarizationMean = 0.0;
        double polarizationStd = 0.0;
        double avgOpenness = 0.0;
        double avgConformity = 0.0;
        // Economy metrics
        double globalWelfare = 1.0;
        double globalInequality = 0.0;
        double globalHardship = 0.0;
    };
    Metrics computeMetrics() const;
    
    // Detailed Statistics (for probing/analysis)
    struct Statistics {
        // Population
        std::uint32_t totalAgents = 0;
        std::uint32_t aliveAgents = 0;
        
        // Demographics by age group
        std::uint32_t children = 0;        // 0-14
        std::uint32_t youngAdults = 0;     // 15-29
        std::uint32_t middleAge = 0;       // 30-49
        std::uint32_t mature = 0;          // 50-69
        std::uint32_t elderly = 0;         // 70+
        
        // Gender
        std::uint32_t males = 0;
        std::uint32_t females = 0;
        
        // Age statistics
        double avgAge = 0.0;
        int minAge = 0;
        int maxAge = 0;
        
        // Network
        double avgConnections = 0.0;
        std::uint32_t isolatedAgents = 0;
        
        // Beliefs
        double polarizationMean = 0.0;
        double polarizationStd = 0.0;
        std::array<double, 4> avgBeliefs = {0, 0, 0, 0};
        
        // Regional distribution
        std::uint32_t occupiedRegions = 0;
        double avgPopPerRegion = 0.0;
        std::uint32_t minRegionPop = 0;
        std::uint32_t maxRegionPop = 0;
        
        // Economy
        double globalWelfare = 1.0;
        double globalInequality = 0.0;
        double avgIncome = 0.0;
        
        // Languages
        std::array<std::uint32_t, 256> langCounts = {0};
        std::uint8_t numLanguages = 0;
    };
    Statistics getStatistics() const;
    
private:
    void initAgents();
    void buildSmallWorld();
    void updateBeliefs();
    
    // Demography
    void stepDemography();
    void stepMigration();  // Migration decisions
    void createChild(std::uint32_t motherId);
    void compactDeadAgents();
    double mortalityRate(int age) const;
    double mortalityPerTick(int age) const;
    double mortalityPerTick(int age, std::uint32_t region_id) const;  // Region-specific mortality
    double fertilityRateAnnual(int age) const;
    double fertilityPerTick(int age) const;
    double fertilityPerTick(int age, std::uint32_t region_id, const Agent& agent,
                           const std::array<double, 4>& region_beliefs) const;  // Region and agent-specific fertility
    
    // Language assignment based on region geography
    void assignLanguagesByGeography();
    
    // Language dynamics: prestige-based language shift
    void updateLanguageDynamics();
    
    // Network reconnection for isolated agents (prevents network decay)
    void reconnectIsolatedAgents();
    void formLocalConnections(std::size_t agent_idx, int max_new_connections = 3);
    
    // Incremental regional aggregates (avoids O(N) recomputation)
    void updateRegionalAggregates();
    void onAgentBorn(std::uint32_t agent_id);
    void onAgentDied(std::uint32_t agent_id);
    void onAgentMigrated(std::uint32_t agent_id, std::uint32_t from_region, std::uint32_t to_region);
    void rebuildRegionalAggregates();  // Full rebuild (used at init and periodically for correction)

    KernelConfig cfg_;
    std::vector<Agent> agents_;
    std::vector<std::vector<std::uint32_t>> regionIndex_;  // region -> agent IDs
    std::uint64_t generation_ = 0;
    std::mt19937_64 rng_;
    Economy economy_;  // Economic module
    PsychologyModule psychology_;
    HealthModule health_;
    MeanFieldApproximation mean_field_;  // Mean field approximation
    EventLog event_log_;  // Event tracking system
    
    // Incrementally maintained regional aggregates
    struct RegionalAggregates {
        std::uint32_t population = 0;
        std::array<double, 4> belief_sum = {0.0, 0.0, 0.0, 0.0};
        bool dirty = false;  // Set if incremental updates may have drifted
    };
    std::vector<RegionalAggregates> regional_aggregates_;
    bool aggregates_initialized_ = false;
    
    // Pre-computed migration attractiveness (updated periodically, not per-migrant)
    std::vector<double> region_attractiveness_;
    std::vector<std::uint32_t> sorted_attractive_regions_;  // Indices sorted by attractiveness (desc)
    std::uint64_t attractiveness_update_gen_ = 0;

    // Helper functions
    inline double fastTanh(double x) const {
        // Padé approximant for tanh: (x * (27 + x^2)) / (27 + 9 * x^2)
        // This is a fast, reasonably accurate approximation.
        double x2 = x * x;
        return x * (27.0 + x2) / (27.0 + 9.0 * x2);
    }

    inline double similarityGate(const Agent& a, const Agent& b) const {
        // Cosine similarity: (a . b) / (||a|| * ||b||)
        // We use cached squared norms to avoid sqrt.
        double dot = a.B[0] * b.B[0] + a.B[1] * b.B[1] + a.B[2] * b.B[2] + a.B[3] * b.B[3];
        
        double norm_prod_sq = a.B_norm_sq * b.B_norm_sq;
        
        if (norm_prod_sq < 1e-9) {
            return 1.0; // Both vectors are near-zero, consider them similar
        }
        
        // similarity = cos(theta)
        double sim = dot / std::sqrt(norm_prod_sq);
        
        // Simple linear gate: 0 if sim < floor, 1 if sim > 1, linear in between
        return std::max(0.0, (sim - cfg_.simFloor) / (1.0 - cfg_.simFloor));
    }

    inline double languageQuality(const Agent& a, const Agent& b) const {
        if (a.primaryLang == b.primaryLang) {
            return 0.5 * (a.fluency + b.fluency);
        }
        return 0.1; // Low quality for different languages
    }
};

#endif // KERNEL_H
