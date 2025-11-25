# Civilization Simulation Engine

A high-performance agent-based simulation engine for modeling large-scale social dynamics. The engine simulates emergent behaviors from individual agents with beliefs, personalities, and social networks.

## Architecture

The project is structured into two distinct layers:

### Core Engine (`core/`)
A game-agnostic simulation kernel that provides:
- **Agent System**: 50,000+ agents with 4D belief dynamics, personality traits, and social networks
- **Economy Module**: 5-good trade system with regional specialization, dynamic pricing, and wealth distribution
- **Demographics**: Age-structured population with births, deaths, and migration
- **Culture Detection**: K-means and DBSCAN clustering for emergent cultural groupings
- **Network Topology**: Watts-Strogatz small-world networks for realistic social connections
- **Performance Optimizations**: Mean-field approximation, cohort-based demographics, matrix trade diffusion

### Game Layer (`game/`)
Application-specific modules that build on the core engine:
- **Movement Module**: Political/social movement formation, lifecycle, and power dynamics
- *(Future: Institutions, Diplomacy, War, etc.)*

### Emergent Systems
18 hardcoded behaviors replaced with truly emergent dynamics:
- Inequality computed from actual Gini coefficient, not system labels
- Economic efficiency emerges from development + moderate inequality
- Trade partners based on geographic distance (2-15 partners)
- Regional subsistence needs vary by climate/development
- Stress sensitivity varies by personality traits
- Demographic rates modified by regional development

See [docs/EMERGENT-SYSTEMS.md](docs/EMERGENT-SYSTEMS.md) for technical details.

---

## Quick Start

### Docker (Recommended)
```bash
docker compose up kernel                    # Interactive mode
docker compose --profile batch up           # Batch mode (1000 ticks)
```

### Native Build

**Windows (Visual Studio 2022):**
```cmd
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022"
cmake --build . --config Release
Release\KernelSim.exe
```

**Linux/macOS:**
```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
./KernelSim
```

---

## Usage

**Interactive CLI:**
```
> reset 50000 200      # 50k agents, 200 regions
> run 1000 10          # 1000 ticks, log every 10
> economy              # Global economy summary
> region 42            # Regional economy details
> cluster kmeans 5     # Detect 5 cultural clusters
> cultures             # Show detected clusters
> stats                # Detailed demographics & network stats
> state traits         # Export JSON snapshot with traits
> quit
```

**Batch Mode:**
```bash
echo "run 5000 100" | ./KernelSim
```

---

## Project Structure

```
core/                  # Core simulation engine (game-agnostic)
├── include/
│   ├── kernel/        # Kernel.h - Main simulation engine
│   ├── modules/       # Economy, Culture, Health, Psychology, etc.
│   ├── io/            # Snapshot export utilities
│   └── utils/         # Helpers, event logging
├── src/               # Implementation files
└── third_party/       # httplib.h

game/                  # Game-specific modules
├── include/modules/   # Movement.h, etc.
└── src/modules/       # Movement.cpp, etc.

cli/                   # Command-line interface
tests/                 # Unit and integration tests
docker/                # Docker Compose configurations
docs/                  # Design documentation
data/                  # Simulation outputs
```

---

## Core Engine Features

### Agent Model
Each agent has:
- **Beliefs** (4D space, -1 to 1): Authority↔Liberty, Tradition↔Progress, Hierarchy↔Equality, Faith↔Reason
- **Personality traits** (0-1): Openness, Conformity, Assertiveness, Sociality
- **Demographics**: Age (0-90 years), gender, lineage tracking
- **Language**: Primary language with fluency level
- **Network**: Social connections via small-world topology

### Belief Dynamics
- Neighbors influence beliefs based on similarity and personality
- Mean-field approximation for O(N) scaling (vs O(N×k) for direct neighbor iteration)
- Personality modulates susceptibility and influence strength

### Economy
- **5 Goods**: Food, Energy, Tools, Luxury, Services
- **Regional Specialization**: Resource endowments drive trade patterns
- **Dynamic Pricing**: Supply/demand equilibration
- **Metrics**: Welfare, Gini coefficient, hardship

### Performance
| Population | Time/Tick | Memory |
|-----------|-----------|--------|
| 50,000    | ~15ms     | ~20MB  |
| 100,000   | ~30ms     | ~40MB  |
| 1,000,000 | ~200ms    | ~400MB |

---

## Configuration

```cpp
struct KernelConfig {
    uint32_t population = 50000;
    uint32_t regions = 200;
    uint32_t avgConnections = 8;    // Network degree
    double rewireProb = 0.05;       // Small-world rewiring
    double stepSize = 0.15;         // Belief update rate
    bool useMeanField = true;       // Use O(N) approximation
    bool demographyEnabled = true;  // Enable births/deaths
    int ticksPerYear = 10;          // Age granularity
};
```

---

## Documentation

- **[docs/DESIGN.md](docs/DESIGN.md)** - System architecture and module specifications
- **[docs/FEATURES.md](docs/FEATURES.md)** - Detailed feature documentation
- **[docs/EMERGENT-SYSTEMS.md](docs/EMERGENT-SYSTEMS.md)** - Emergent dynamics refactoring (18 fixes)
- **[docs/OPTIMIZATION-GUIDE.md](docs/OPTIMIZATION-GUIDE.md)** - Performance optimization techniques
- **[docs/DOCKER.md](docs/DOCKER.md)** - Container deployment guide
- **[CHANGELOG.md](CHANGELOG.md)** - Version history

---

## Building Options

```bash
cmake .. -DBUILD_TESTS=ON          # Include test suite
cmake .. -DBUILD_GAME=OFF          # Build only core engine (no game modules)
cmake .. -DENABLE_OPENMP=ON        # Enable parallel processing
```

---

## License

See LICENSE file.
