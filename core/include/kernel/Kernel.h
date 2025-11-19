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
#include "modules/Movement.h"
#include "utils/EventLog.h"

// ---------- Configuration ----------
struct KernelConfig {
    std::uint32_t population = 50000;
    std::uint32_t regions = 200;
    std::uint32_t avgConnections = 8;  // k (even)
    double rewireProb = 0.05;           // p for Watts-Strogatz
    double stepSize = 0.15;             // eta (global influence rate)
    double simFloor = 0.05;             // minimum similarity gate
    std::uint64_t seed = 42;
    std::string startCondition = "baseline"; // economic starting profile
    
    // Demography
    int ticksPerYear = 10;              // age granularity
    int maxAgeYears = 90;               // hard cap on lifespan
    double regionCapacity = 500.0;      // target population per region
    bool demographyEnabled = true;      // enable births/deaths
};

struct BeliefModifier {
    double authority_delta = 0.0;
    double tradition_delta = 0.0;
    double hierarchy_delta = 0.0;
    double isolation_delta = 0.0;
};

struct EconomicSystemDef {
    std::string name;
    BeliefModifier modifiers;
};

#include "kernel/Agent.h"
#include "kernel/AgentStorage.h"

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
    
    // Movement access
    const MovementModule& movements() const { return movements_; }
    MovementModule& movementsMut() { return movements_; }
    
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

    KernelConfig cfg_;
    std::vector<Agent> agents_;

    // The new high-performance storage
    AgentStorage gpu_storage_; 
    
    // The new optimized update function
    void updateBeliefsSoA(); 

    std::unordered_map<std::string, EconomicSystemDef> econ_systems_;
    void initEconomicSystems();

    std::vector<std::vector<std::uint32_t>> regionIndex_;  // region -> agent IDs
    std::uint64_t generation_ = 0;
    std::mt19937_64 rng_;
    Economy economy_;  // Economic module
    PsychologyModule psychology_;
    HealthModule health_;
    MovementModule movements_;  // Movement module
    EventLog event_log_;  // Event tracking system

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
