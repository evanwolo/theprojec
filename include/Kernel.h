#ifndef KERNEL_H
#define KERNEL_H

#include <array>
#include <vector>
#include <cstdint>
#include <string>
#include <random>

// ---------- Configuration ----------
struct KernelConfig {
    std::uint32_t population = 50000;
    std::uint32_t regions = 200;
    std::uint32_t avgConnections = 8;  // k (even)
    double rewireProb = 0.05;           // p for Watts-Strogatz
    double stepSize = 0.15;             // eta (global influence rate)
    double simFloor = 0.05;             // minimum similarity gate
    std::uint64_t seed = 42;
};

// ---------- Agent Structure ----------
struct Agent {
    // Identity
    std::uint32_t id = 0;
    std::uint32_t region = 0;
    
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
    
    // Module multipliers (written by tech/media/economy modules)
    double m_comm = 1.0;        // communication reach/speed
    double m_susceptibility = 1.0;  // influence susceptibility
    double m_mobility = 1.0;    // migration/relocation ease
    
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
    
    // Metrics (lightweight for logging)
    struct Metrics {
        double polarizationMean = 0.0;
        double polarizationStd = 0.0;
        double avgOpenness = 0.0;
        double avgConformity = 0.0;
    };
    Metrics computeMetrics() const;
    
private:
    KernelConfig cfg_;
    std::vector<Agent> agents_;
    std::vector<std::vector<std::uint32_t>> regionIndex_;  // region -> agent IDs
    std::uint64_t generation_ = 0;
    std::mt19937_64 rng_;
    
    void initAgents();
    void buildSmallWorld();
    void updateBeliefs();
    
    // Helper functions
    double languageQuality(const Agent& i, const Agent& j) const;
    double similarityGate(const Agent& i, const Agent& j) const;
    
    // Fast tanh approximation for performance (accurate in [-3, 3])
    inline double fastTanh(double x) const {
        // Clamp to avoid overflow
        if (x < -3.0) return -1.0;
        if (x > 3.0) return 1.0;
        // Rational approximation: tanh(x) ≈ x * (27 + x²) / (27 + 9*x²)
        const double x2 = x * x;
        return x * (27.0 + x2) / (27.0 + 9.0 * x2);
    }
};

#endif
