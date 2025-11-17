# Grand Strategy Simulation Engine

A nation-scale grand strategy game where societies evolve bottom-up from millions of agents; high-level structures—cultures, movements, institutions, ideologies, media regimes, tech regimes, and wars—emerge, compete, ossify, collapse, and reform over centuries without scripts.

## Quick Start

See [docs/QUICKSTART.md](docs/QUICKSTART.md) for setup and basic usage.

## Documentation

- [Design Overview](docs/DESIGN.md) - System architecture and vision
- [Status & Roadmap](docs/STATUS.md) - Current implementation status
- [Docker Guide](docs/DOCKER.md) - Container deployment
- [API Reference](docs/API.md) - Module interfaces

## Project Structure

```
├── core/          # Core simulation engine (static library)
├── cli/           # Command-line interfaces
├── tests/         # Unit and integration tests
├── tools/         # Utilities and visualizers
├── scenarios/     # Pre-made scenario configurations
├── mods/          # User-created content and mods
├── legacy/        # Original prototype (archived)
├── docker/        # Container configurations
├── docs/          # Documentation
└── scripts/       # Build and CI scripts
```

## Building

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

## Running

```bash
# Interactive mode
./KernelSim

# Batch processing
echo "run 1000 50" | ./KernelSim
```

## License

See LICENSE file.

## Building

### Native Build

```bash
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

**Windows (Visual Studio):**
```powershell
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022"
cmake --build . --config Release
```

### Docker Build

```bash
# Build the Docker image
docker build -t grand-strategy-kernel:latest .

# Run interactively
docker run -it --rm -v $(pwd)/data:/app/data grand-strategy-kernel:latest

# Run batch simulation
echo "run 1000 10" | docker run -i --rm -v $(pwd)/data:/app/data grand-strategy-kernel:latest

# Using Docker Compose
docker compose up kernel
```

See [`DOCKER.md`](DOCKER.md) for complete Docker deployment guide.

## Running

### Interactive Mode
```bash
# Windows PowerShell
.\build\Release\KernelSim.exe

# Linux/macOS
./build/KernelSim
```

### Batch Mode (1000 ticks, log every 10 steps)
```bash
echo "run 1000 10" | .\build\Release\KernelSim.exe
```

## Commands

- `step N` - Advance simulation by N steps and output JSON snapshot
- `state [traits]` - Print current state as JSON (optional: include personality traits)
- `metrics` - Print current polarization and trait statistics
- `reset [N R k p]` - Reset with optional parameters:
  - `N`: population size (default: 50000)
  - `R`: number of regions (default: 200)
  - `k`: average connections per node (default: 8)
  - `p`: rewiring probability (default: 0.05)
- `run T log` - Run T ticks, logging metrics every `log` steps to `data/metrics.csv`
- `quit` - Exit the simulation
- `help` - Show command help

## Example Session

```
# Check initial state
metrics

# Run 100 steps and export
step 100

# Run longer batch with logging
run 1000 10

# Reset with larger population
reset 100000 400 10 0.05

# Run and export final state with traits
run 500 50
state traits
quit
```

## JSON Output Format

```json
{
  "generation": 500,
  "metrics": {
    "polarizationMean": 1.2347,
    "polarizationStd": 0.3421,
    "avgOpenness": 0.5012,
    "avgConformity": 0.4987
  },
  "agents": [
    {
      "id": 0,
      "region": 42,
      "lang": 2,
      "beliefs": [0.2149, -0.1380, 0.5903, -0.2201],
      "traits": {
        "openness": 0.5234,
        "conformity": 0.4876,
        "assertiveness": 0.5123,
        "sociality": 0.4932
      }
    }
  ]
}
```

## CSV Metrics Format

```csv
generation,polarization_mean,polarization_std,avg_openness,avg_conformity
0,1.1234,0.3012,0.5001,0.4998
10,1.1456,0.3087,0.5003,0.4996
20,1.1678,0.3142,0.5005,0.4994
```

## Architecture

```
include/
├── Kernel.h           # Core agent-based simulation engine
├── KernelSnapshot.h   # JSON/CSV export utilities
├── BeliefTypes.h      # (Legacy) Type definitions
├── Individual.h       # (Legacy) Original prototype
├── Network.h          # (Legacy) Original network builder
├── Simulation.h       # (Legacy) Original simulation class
└── Snapshot.h         # (Legacy) Original JSON export

src/
├── Kernel.cpp         # High-performance kernel implementation
├── KernelSnapshot.cpp # Export and logging
├── main_kernel.cpp    # CLI for kernel
└── main.cpp           # (Legacy) Original CLI

data/                  # Output directory for metrics.csv
```

## Integration Points for Modules

The kernel provides clean integration hooks for Phase 2+ modules:

### Agent Fields
```cpp
// Already present:
double m_comm;              // Communication reach/speed multiplier (tech/media)
double m_susceptibility;    // Influence susceptibility (personality, stress, drugs)
double m_mobility;          // Migration ease (infrastructure, economy)
int32_t parent_a, parent_b; // Lineage tracking
uint32_t lineage_id;        // Clan/house/family ID
```

### Access Methods
```cpp
// Mutable access for modules:
std::vector<Agent>& agentsMut();  // Update multipliers, lineage, language
const std::vector<std::vector<uint32_t>>& regionIndex();  // Spatial queries
```

### Example Module Integration (Pseudocode)
```cpp
// Tech module updates communication multipliers
for (auto& agent : kernel.agentsMut()) {
    if (agent.region has_tech("radio")) {
        agent.m_comm *= 1.5;
    }
}

// Economy module updates susceptibility via stress
for (auto& agent : kernel.agentsMut()) {
    double hardship = economy.getHardship(agent.region);
    agent.m_susceptibility = 0.7 + 0.5 * hardship;
}

// Media module reads beliefs to compute narratives
for (const auto& agent : kernel.agents()) {
    media.processBelief(agent.id, agent.B, agent.primaryLang);
}
```

## Performance

- **50,000 agents**: ~50ms per tick (single-threaded, -O3 -march=native)
- **Memory**: ~8MB for agents + ~2MB for network (8 neighbors/agent)
- **Scalability**: Parallel-friendly design; add OpenMP pragmas for multi-core

## Integration with Web Clients

The CLI uses stdin/stdout, making it easy to integrate with:
- Node.js child processes
- WebSocket servers
- React/browser clients
- Batch processing pipelines

## Roadmap

### ✅ Phase 1.0: Basic Kernel (Complete)
- ✅ Agent belief system with personality traits
- ✅ Small-world network topology
- ✅ Deterministic belief updates
- ✅ Language and region systems
- ✅ 50k+ agent capacity

### 🔄 Phase 1.5: Metrics & Clustering (Next)
- Culture/movement clustering from belief space (k-means, DBSCAN)
- Enhanced polarization metrics (within-cluster vs. between-cluster)
- Coherence and transmission strength metrics
- Scale to 100k agents with vectorization

### Phase 2: Emergent Politics
- Lineage/kinship layer (parent IDs, clan IDs, elite continuity)
- Decision module (bounded utility: protest, migrate, join movements)
- Institutions with bias, legitimacy, rigidity, capacity
- Resource economy (food/energy/industry, hardship/inequality)
- Technology multipliers (communication, production, repression)
- Media outlets (reach, bias, accuracy, language, ownership)
- Language repertoires and communication quality

### Phase 3: War & Diplomacy
- Front-based war system (no unit micro)
- Logistics (rail/road/sea throughput, supply lines)
- War support from casualties, shortages, media narratives
- Diplomacy (alliances, blocs, sanctions, guarantees)
- Event windows with causal triggers and phase hints

### Phase 4: Ontology & Tuning
- Prime-number phase tagging (Duality, Crisis, Shadow, etc.)
- UI overlays (culture, movements, fronts, phases)
- Batch tuning and "story probes" for emergent validation
- Performance optimization and telemetry

### Phase 5: Polish & Modding
- Optional AI labeling for ideologies and eras
- Narrative summaries from system state
- Modding hooks for scenarios and archetypes
- Steam integration and user scenarios

## Design Philosophy

**No scripts, only systems**: Cultures, movements, ideologies, and crises emerge from agent interactions, not preset narratives.

**Strategic control, not micromanagement**: Players govern via laws, budgets, diplomacy, and war posture—not tile painting or unit stacks.

**Readable complexity**: A metaphysical phase lens (prime factorization tags) translates systemic chaos into strategic hints.

**Modular architecture**: Clean separation between kernel (agents), culture, institutions, economy, media, war, and ontology enables incremental development.

**Performance-first**: Optimized for 50k–1M agents with parallel-friendly algorithms and cache-efficient data layouts.

See [`DESIGN.md`](DESIGN.md) for complete system specifications.

## Legacy Prototype

The original prototype (`OpinionSim`) remains available for comparison:

```bash
# Windows PowerShell
.\build\Release\OpinionSim.exe

# Linux/macOS
./build/OpinionSim
```

This was the initial 200-agent proof-of-concept; the new `KernelSim` is the production implementation.
