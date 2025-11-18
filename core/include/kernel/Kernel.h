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

// ---------- Configuration ----------
struct KernelConfig {
    std::uint32_t population = 50000;
    std::uint32_t regions = 200;
    std::uint32_t avgConnections = 8;  // k (even)
    double rewireProb = 0.05;           // p for Watts-Strogatz
    double stepSize = 0.15;             // eta (global influence rate)
    double simFloor = 0.05;             // minimum similarity gate
    std::uint64_t seed = 42;
    
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
    
    // Language (primary for kernel; extend to repertoire in Phase 2)
    std::uint8_t primaryLang = 0;
    double fluency = 1.0;  // 0..1
    
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
    
    // Movement access
    const MovementModule& movements() const { return movements_; }
    MovementModule& movementsMut() { return movements_; }
    
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
    
private:
    void initAgents();
    void buildSmallWorld();
    void updateBeliefs();
    
    // Demography
    void stepDemography();
    void createChild(std::uint32_t motherId);
    void compactDeadAgents();
    double mortalityRate(int age) const;
    double mortalityPerTick(int age) const;
    double fertilityRateAnnual(int age) const;
    double fertilityPerTick(int age) const;

    KernelConfig cfg_;
    std::vector<Agent> agents_;
    std::vector<std::vector<std::uint32_t>> regionIndex_;  // region -> agent IDs
    std::uint64_t generation_ = 0;
    std::mt19937_64 rng_;
    Economy economy_;  // Economic module
    PsychologyModule psychology_;
    HealthModule health_;
    MovementModule movements_;  // Movement module

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
