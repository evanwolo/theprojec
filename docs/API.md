# API Documentation

Complete API reference for the Emergent Civilization Simulation Engine.

## Table of Contents

- [Core Kernel API](#core-kernel-api)
- [Agent Structure](#agent-structure)
- [Configuration](#configuration)
- [Modules API](#modules-api)
- [Query Interface](#query-interface)
- [Event System](#event-system)

---

## Core Kernel API

### Kernel Class

The main simulation engine orchestrating all subsystems.

```cpp
#include "kernel/Kernel.h"

class Kernel {
public:
    explicit Kernel(const KernelConfig& cfg);
    
    // Lifecycle Management
    void reset(const KernelConfig& cfg);
    void step();
    void stepN(int n);
    
    // State Access (const)
    const std::vector<Agent>& agents() const;
    const std::vector<std::vector<uint32_t>>& regionIndex() const;
    uint64_t generation() const;
    const Economy& economy() const;
    EventLog& eventLog();
    
    // Mutable Access (use with caution)
    std::vector<Agent>& agentsMut();
    Economy& economyMut();
    
    // Metrics & Statistics
    Metrics computeMetrics() const;
    Statistics getStatistics() const;
};
```

#### Lifecycle Methods

**`Kernel(const KernelConfig& cfg)`**
- Constructs kernel with specified configuration
- Initializes all subsystems (economy, psychology, health, demographics)
- Seeds RNG and builds initial agent population
- **Throws:** `std::invalid_argument` if config validation fails

**`void reset(const KernelConfig& cfg)`**
- Resets simulation to initial state with new configuration
- Destroys all existing agents and regenerates population
- Reseeds RNG and rebuilds network topology
- **Use Case:** Running multiple simulation scenarios without recreating kernel

**`void step()`**
- Advances simulation by one tick
- **Order of operations:**
  1. Update beliefs (social influence)
  2. Demographic updates (births/deaths)
  3. Migration (every 10 ticks)
  4. Economic update (every 10 ticks)
  5. Language dynamics (every 50 ticks)
  6. Psychology and health module updates
- **Performance:** ~15ms for 50k agents, scales O(N)

**`void stepN(int n)`**
- Batch advances simulation by n ticks
- More efficient than calling `step()` n times due to reduced overhead
- **Use Case:** Long simulation runs without intermediate output

---

## Agent Structure

### Agent

Represents a single simulated individual with beliefs, demographics, personality, and social connections.

```cpp
struct Agent {
    // Identity
    uint32_t id;              // Unique identifier (stable across lifetime)
    uint32_t region;          // Current region (0 to num_regions-1)
    bool alive;               // Dead agents marked false, compacted periodically
    
    // Demographics
    int age;                  // Age in years (0 to maxAgeYears)
    bool female;              // Sex (50/50 distribution at birth)
    int32_t parent_a;         // Mother ID (-1 if initial generation)
    int32_t parent_b;         // Father ID (-1 if unknown/initial)
    uint32_t lineage_id;      // Matrilineal lineage tracking
    
    // Language
    uint8_t primaryLang;      // Language family (0-3)
    uint8_t dialect;          // Regional dialect variant (0-9)
    double fluency;           // Fluency level (0.0-1.0)
    
    // Personality Traits (0.0-1.0, mean ~0.5)
    double openness;          // Receptiveness to new ideas
    double conformity;        // Desire to match group norms
    double assertiveness;     // Confidence in self-expression
    double sociality;         // Desire for social connections
    
    // Belief State (4D political/cultural space)
    array<double, 4> x;       // Internal state (unbounded)
    array<double, 4> B;       // Observable beliefs [-1, 1]
                              // [0] Authority ↔ Liberty
                              // [1] Tradition ↔ Progress
                              // [2] Hierarchy ↔ Equality
                              // [3] Faith ↔ Reason
    double B_norm_sq;         // Cached ||B||² for performance
    
    // Module Multipliers (updated by subsystems)
    double m_comm;            // Communication reach/speed
    double m_susceptibility;  // Influence susceptibility
    double m_mobility;        // Migration ease
    
    // Module States
    PsychologicalState psych; // Stress, burnout, resilience
    HealthState health;       // Vitality, disease risk
    
    // Social Network
    vector<uint32_t> neighbors; // Social connections (Watts-Strogatz topology)
};
```

#### Key Design Notes

**Belief Representation:**
- Internal state `x` is unbounded (updated via differential equations)
- Observable beliefs `B = tanh(x)` constrained to [-1, 1]
- This prevents belief saturation while maintaining bounded output

**Network Topology:**
- Small-world network (Watts-Strogatz model)
- Average degree configurable (default: 8 connections)
- Rewiring probability creates long-range shortcuts
- Isolated agents reconnect automatically every 5 ticks

**Module Multipliers:**
- `m_comm`: Technology, media access affect information flow
- `m_susceptibility`: Openness, hardship affect influence receptiveness
- `m_mobility`: Sociality, age, network size affect migration

---

## Configuration

### KernelConfig

Primary configuration structure for simulation initialization.

```cpp
struct KernelConfig {
    // Population & Geography
    uint32_t population = 50000;      // Total agent count
    uint32_t regions = 200;           // Number of regions
    
    // Network Topology (Watts-Strogatz)
    uint32_t avgConnections = 8;      // k (must be even)
    double rewireProb = 0.05;         // p (0=ring, 1=random)
    
    // Belief Dynamics
    double stepSize = 0.15;           // η (influence rate)
    double simFloor = 0.05;           // Minimum similarity gate
    bool useMeanField = true;         // Use O(N) approximation
    
    // Demographics
    int ticksPerYear = 10;            // Time granularity
    int maxAgeYears = 90;             // Hard age cap
    double regionCapacity = 500.0;    // Target population/region
    bool demographyEnabled = true;    // Enable births/deaths
    
    // Initialization
    uint64_t seed = 42;               // RNG seed
    string startCondition = "baseline"; // Economic starting profile
};
```

#### Configuration Validation

The kernel validates configuration on construction:

```cpp
// Throws std::invalid_argument on validation failure
Kernel kernel(config); 

// Validation checks:
// - ticksPerYear > 0
// - maxAgeYears > 0
// - regionCapacity > 0
// - avgConnections is even
// - 0 <= rewireProb <= 1
// - 0 < stepSize < 1
```

#### Start Conditions

**Available profiles:**
- `"baseline"`: Balanced initial conditions
- `"unequal"`: High initial inequality
- `"developed"`: High initial development
- `"feudal"`: Low development, high inequality

Each profile configures:
- Base development levels
- Endowment distributions
- Initial economic systems
- Agent wealth distributions

---

## Modules API

### Economy Module

Manages regional production, trade, consumption, and agent-level economics.

```cpp
class Economy {
public:
    // Initialization
    void init(uint32_t num_regions, 
              uint32_t num_agents,
              mt19937_64& rng,
              const string& start_condition);
    
    // Update (called every 10 ticks by kernel)
    void update(const vector<uint32_t>& region_populations,
                const vector<array<double, 4>>& region_belief_centroids,
                const vector<Agent>& agents,
                uint64_t generation,
                const vector<vector<uint32_t>>* region_index = nullptr);
    
    // Regional Access
    const RegionalEconomy& getRegion(uint32_t region_id) const;
    RegionalEconomy& getRegionMut(uint32_t region_id);
    
    // Agent Access
    const AgentEconomy& getAgentEconomy(uint32_t agent_id) const;
    AgentEconomy& getAgentEconomy(uint32_t agent_id);
    
    // Agent Lifecycle
    void addAgent(uint32_t agent_id, uint32_t region_id, mt19937_64& rng);
    
    // Global Metrics
    double globalWelfare() const;
    double globalInequality() const;
    double globalHardship() const;
    double globalDevelopment() const;
    
    // Trade Analysis
    const vector<TradeLink>& getTradeLinks() const;
    double getTotalTrade() const;
};
```

#### RegionalEconomy Structure

```cpp
struct RegionalEconomy {
    uint32_t region_id;
    
    // Geography (0.0-1.0 normalized coordinates)
    double x, y;
    
    // Production System (5 goods: Food, Energy, Tools, Luxury, Services)
    array<double, 5> endowments;      // Base capacity
    array<double, 5> specialization;  // Focus level
    array<double, 5> production;      // Current output
    array<double, 5> consumption;     // Current usage
    array<double, 5> prices;          // Relative values
    array<double, 5> trade_balance;   // Net exports
    
    // Economic Indicators
    double welfare;           // Consumption per capita
    double inequality;        // Gini coefficient (0-1)
    double hardship;          // Unmet basic needs (0-1)
    double development;       // Accumulated capital (0-5+)
    
    // System Emergence
    string economic_system;   // "market", "planned", "mixed", etc.
    double system_stability;  // Belief-system alignment (0-1)
    double institutional_inertia; // Resistance to change (0-1)
    
    // Language
    array<double, 4> language_prestige;
    uint8_t dominant_language;
    double linguistic_diversity;
};
```

#### AgentEconomy Structure

```cpp
struct AgentEconomy {
    double wealth;        // Accumulated assets
    double income;        // Income per tick
    double productivity;  // Personal multiplier
    int sector;           // Which good produced (0-4)
    double hardship;      // Personal economic stress (0-1)
};
```

---

### Psychology Module

Tracks agent psychological state influenced by social and economic conditions.

```cpp
class PsychologyModule {
public:
    void configure(uint32_t regions, uint64_t seed);
    void initializeAgents(vector<Agent>& agents);
    void updateAgents(vector<Agent>& agents, 
                      const Economy& economy,
                      uint64_t generation);
};

struct PsychologicalState {
    double stress;       // Accumulated stress (0-1)
    double burnout;      // Chronic exhaustion (0-1)
    double resilience;   // Stress resistance (0-1)
};
```

**Stress Sources:**
- Economic hardship (agent and regional)
- Social isolation (low network connectivity)
- Belief-environment mismatch
- Age-related factors

**Effects:**
- Increased `m_susceptibility` to radical beliefs
- Reduced social engagement
- Migration propensity changes

---

### Health Module

Manages agent vitality and disease risk.

```cpp
class HealthModule {
public:
    void configure(uint32_t regions, uint64_t seed);
    void initializeAgents(vector<Agent>& agents);
    void updateAgents(vector<Agent>& agents,
                      const Economy& economy,
                      uint64_t generation);
};

struct HealthState {
    double vitality;     // Overall health (0-1)
    double disease_risk; // Susceptibility (0-1)
};
```

**Vitality Factors:**
- Age (declines with age)
- Regional welfare and development
- Personal wealth
- Stress levels

**Disease Risk Factors:**
- Regional population density
- Development level (sanitation)
- Personal health habits (personality-dependent)

---

### MeanField Approximation

Optimizes belief updates from O(N×k) to O(N) while maintaining emergent polarization.

```cpp
class MeanFieldApproximation {
public:
    void configure(uint32_t regions);
    
    void computeFields(const vector<Agent>& agents,
                       const vector<vector<uint32_t>>& regionIndex);
    
    array<double, 4> getBlendedInfluence(
        const NeighborInfluence& neighbor_influence,
        uint32_t region_id,
        double neighbor_weight) const;
};

struct NeighborInfluence {
    array<double, 4> belief_sum;
    double total_weight;
    int neighbor_count;
};
```

**How It Works:**
1. Compute regional mean beliefs (O(N))
2. For each agent, blend neighbor influence with regional field
3. `neighbor_weight` (0-1) controls echo chamber vs mainstream pull
4. Non-conformists weight neighbors high (form subcultures)
5. Conformists weight regional field high (follow mainstream)

**Tradeoffs:**
- 10x faster than pairwise updates
- Maintains polarization and echo chambers
- Slight smoothing of very local dynamics
- Can toggle via `KernelConfig::useMeanField`

---

## Query Interface

### Metrics

Lightweight metrics for performance monitoring and logging.

```cpp
struct Kernel::Metrics {
    double polarizationMean;  // Average pairwise distance
    double polarizationStd;   // Polarization variance
    double avgOpenness;       // Mean personality trait
    double avgConformity;     // Mean personality trait
    
    // Economy
    double globalWelfare;
    double globalInequality;
    double globalHardship;
};

Metrics m = kernel.computeMetrics();
```

**Performance:** O(R²) for polarization (pairwise regional centroids), O(N) for traits.

---

### Statistics

Comprehensive statistics for analysis and debugging.

```cpp
struct Kernel::Statistics {
    // Population
    uint32_t totalAgents;
    uint32_t aliveAgents;
    
    // Age Demographics
    uint32_t children;      // 0-14
    uint32_t youngAdults;   // 15-29
    uint32_t middleAge;     // 30-49
    uint32_t mature;        // 50-69
    uint32_t elderly;       // 70+
    
    // Gender
    uint32_t males, females;
    
    // Age Statistics
    double avgAge;
    int minAge, maxAge;
    
    // Network
    double avgConnections;
    uint32_t isolatedAgents;
    
    // Beliefs
    double polarizationMean, polarizationStd;
    array<double, 4> avgBeliefs;
    
    // Regional
    uint32_t occupiedRegions;
    double avgPopPerRegion;
    uint32_t minRegionPop, maxRegionPop;
    
    // Economy
    double globalWelfare;
    double globalInequality;
    double avgIncome;
    
    // Language
    array<uint32_t, 256> langCounts;
    uint8_t numLanguages;
};

Statistics stats = kernel.getStatistics();
```

**Performance:** O(N) scan of all agents. Use sparingly (every 100+ ticks).

---

## Event System

### EventLog

Tracks demographic events for analysis and replay.

```cpp
class EventLog {
public:
    void logBirth(uint64_t tick, uint32_t agent_id, 
                  uint32_t region, uint32_t mother_id);
    void logDeath(uint64_t tick, uint32_t agent_id,
                  uint32_t region, int age);
    void logMigration(uint64_t tick, uint32_t agent_id,
                      uint32_t from_region, uint32_t to_region);
    
    const vector<Event>& events() const;
    void clear();
};

enum class EventType {
    Birth,
    Death,
    Migration
};

struct Event {
    uint64_t tick;
    EventType type;
    uint32_t agent_id;
    uint32_t region;
    // Type-specific data in union
};
```

**Access via Kernel:**
```cpp
EventLog& log = kernel.eventLog();
for (const auto& event : log.events()) {
    if (event.type == EventType::Birth) {
        // Process birth event
    }
}
```

**Performance:** Events stored in `std::vector`, cleared manually to avoid unbounded growth.

---

## Advanced Usage

### Custom Module Integration

To add custom game modules:

1. **Create module in `game/include/modules/`:**
```cpp
class CustomModule {
public:
    void init(Kernel& kernel);
    void update(Kernel& kernel, uint64_t tick);
};
```

2. **Instantiate in game layer:**
```cpp
Kernel kernel(config);
CustomModule custom;
custom.init(kernel);

while (running) {
    kernel.step();
    custom.update(kernel, kernel.generation());
}
```

3. **Access kernel state:**
```cpp
void CustomModule::update(Kernel& kernel, uint64_t tick) {
    const auto& agents = kernel.agents();
    const auto& economy = kernel.economy();
    
    // Your logic here
}
```

### Performance Profiling

**Built-in timing:**
```cpp
#include <chrono>

auto start = chrono::high_resolution_clock::now();
kernel.stepN(1000);
auto end = chrono::high_resolution_clock::now();

auto duration = chrono::duration_cast<chrono::milliseconds>(end - start);
double msPerTick = duration.count() / 1000.0;
```

**Expected performance (50k agents, Release build):**
- Belief update: ~8ms
- Demographics: ~3ms  
- Economy: ~2ms
- Other: ~2ms
- **Total:** ~15ms/tick

### Memory Management

**Memory footprint (50k agents):**
- Agents: ~15MB (300 bytes/agent)
- Economy: ~3MB
- Networks: ~2MB
- **Total:** ~20MB

**Memory growth:**
- Births increase agent count unbounded
- Compaction every 25 ticks removes dead agents from indexes
- Periodic full rebuild (every 100 ticks) prevents aggregate drift

**Manual memory control:**
```cpp
// Force dead agent removal
kernel.compactDeadAgents(); // Not exposed, runs automatically

// Clear event log to prevent growth
kernel.eventLog().clear();
```

---

## Error Handling

### Validation

The kernel performs validation in **debug builds only** via macros:

```cpp
#ifdef ENABLE_VALIDATION
    validation::checkBeliefs(agent.B.data(), 4, "context");
    validation::checkIndex(agent.region, num_regions, "agent.region");
    validation::checkNonNegative(value, "variable_name");
#endif
```

**To enable:** Build with `-DENABLE_VALIDATION` or set in CMake.

### Common Errors

**`std::invalid_argument` on construction:**
- Invalid configuration parameters
- Check `ticksPerYear > 0`, `maxAgeYears > 0`, etc.

**Segmentation faults:**
- Usually invalid agent IDs after network modification
- Enable validation builds to catch index errors

**Performance degradation:**
- Population explosion from high fertility
- Check demographic equilibrium via `Statistics`
- Adjust `regionCapacity` or mortality rates

---

## Thread Safety

### Parallel Sections

The kernel uses OpenMP for parallelization:

```cpp
#pragma omp parallel for schedule(dynamic)
for (size_t i = 0; i < agents.size(); ++i) {
    // Read-only access to other agents is safe
    // Write only to agents[i]
}
```

**Thread-safe operations:**
- Belief updates (writes to separate agents)
- Metric computation (read-only)

**NOT thread-safe:**
- Agent creation/deletion
- Network modification
- Module state updates

All parallel regions handled internally. **External API is single-threaded.**

### RNG Thread Safety

Thread-local RNGs prevent race conditions:

```cpp
namespace {
    thread_local std::mt19937_64 tl_rng{generateThreadSeed()};
}
```

Each thread gets independent RNG. Seeds derived from `random_device + thread_id + counter`.

---

## Example: Complete Simulation Loop

```cpp
#include "kernel/Kernel.h"
#include <iostream>

int main() {
    // Configure simulation
    KernelConfig config;
    config.population = 50000;
    config.regions = 200;
    config.ticksPerYear = 10;
    config.seed = 42;
    config.useMeanField = true;
    
    // Initialize kernel
    Kernel kernel(config);
    
    // Run simulation
    const int totalTicks = 10000;
    const int logInterval = 100;
    
    for (int tick = 0; tick < totalTicks; ++tick) {
        kernel.step();
        
        if (tick % logInterval == 0) {
            auto metrics = kernel.computeMetrics();
            std::cout << "Tick " << tick << ":\n";
            std::cout << "  Polarization: " << metrics.polarizationMean << "\n";
            std::cout << "  Welfare: " << metrics.globalWelfare << "\n";
            std::cout << "  Inequality: " << metrics.globalInequality << "\n";
        }
    }
    
    // Final statistics
    auto stats = kernel.getStatistics();
    std::cout << "\nFinal Statistics:\n";
    std::cout << "  Alive Agents: " << stats.aliveAgents << "\n";
    std::cout << "  Avg Age: " << stats.avgAge << "\n";
    std::cout << "  Languages: " << (int)stats.numLanguages << "\n";
    
    return 0;
}
```

---

## Version History

- **v0.4.0**: Current version with demographics, migration, language dynamics
- **v0.3.0**: Added mean-field approximation, incremental aggregates
- **v0.2.0**: Economy module with trade network
- **v0.1.0**: Initial belief dynamics and network topology

For detailed changelog, see [CHANGELOG.md](../CHANGELOG.md).
