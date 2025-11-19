# Grand Strategy Simulation Engine

A nation-scale grand strategy simulation where societies evolve bottom-up from 50,000+ agents. Cultures, movements, economies, and institutions emerge without scripts. You govern through policy, institutions, and strategic decisions—no micromanagement.

## Current Status (Phase 2.3+)

**✅ Core Systems Implemented:**
- **Kernel:** 50k agents, 4D belief dynamics (Authority/Tradition/Hierarchy/Faith), Watts-Strogatz small-world networks, 200 regions
- **Demographics:** Age-structured population (0-90 years), births/deaths, genetic+cultural inheritance, multi-generational dynamics
- **Economy:** 5-good trade system with dramatic regional specialization and scarcity
  - Goods: Food, Energy, Tools, Luxury, Services
  - Dynamic pricing, regional endowments, trade networks
  - Wealth distribution (Gini coefficient), hardship tracking
  - Economic systems emerge from beliefs: cooperative, mixed, market, feudal, planned
- **Culture:** Charismatic hub detection, identity group formation via clustering (K-means, DBSCAN)
- **Movements:** Political/social movement formation from clusters
  - Formation triggers: size, coherence, charisma density, economic hardship
  - Power metrics: street capacity, charisma, coherence
  - Lifecycle stages: Birth, Growth, Plateau, Schism, Decline, Dead
- **Docker:** Multi-stage build (~150MB image), compose profiles for interactive/batch modes

**✅ Scale & Performance:**
- 50,000 agents with full personality traits, beliefs, networks, economy
- 200 regions with specialized resource endowments
- Trade volume: ~24,000 units/tick at equilibrium
- ~100ms/tick with economy updates every 10 ticks
- Demographics: age-structured population (0-90 years), births/deaths

See [DESIGN.md](docs/DESIGN.md) for architecture and [CHANGELOG.md](CHANGELOG.md) for version history.

---

## Quick Start

### Docker (Recommended)
```bash
# Build and run interactively
docker compose up kernel

# Run batch simulation (1000 ticks, 50k agents)
docker compose --profile batch up

# Data persists in ./data/
```

### Native Build (Windows)
**Visual Studio 2022:**
```cmd
cd e:\theprojec
build.bat
cd build
Release\KernelSim.exe
```

**Manual (Developer Command Prompt):**
```cmd
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
Release\KernelSim.exe
```

### Native Build (Linux/macOS)
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
> economy              # Show global economy summary
> region 42            # Show regional economy details
> cluster kmeans 5     # Detect 5 cultures via K-means
> cultures             # Show detected cultural clusters
> detect_movements     # Detect movements from clusters
> movements            # List active movements
> stats                # Detailed demographics & network stats
> state traits         # Export JSON snapshot with traits
> quit
```

**HTTP Server Mode:**
```bash
# HTTP server not currently implemented
# Use CLI mode for now
```

**Batch Mode:**
```bash
echo "run 5000 100" | ./KernelSim
echo "state traits" | ./KernelSim
```

---

## Project Structure

```
core/              # Core simulation (Kernel, Economy, Culture, Health, Psychology modules)
├── include/       # Public headers
├── src/           # Implementation
└── third_party/   # httplib for REST API

cli/               # Command-line interface and HTTP server
tests/             # Unit and integration tests
docker/            # Docker Compose configurations
docs/              # Design docs
scripts/           # Build automation
data/              # Simulation outputs (snapshots, metrics, logs)
```

---

## Documentation

- **[DESIGN.md](docs/DESIGN.md)** - System architecture, emergent structures, ontology
- **[CHANGELOG.md](CHANGELOG.md)** - Version history and milestones (Phase 2.3 current)
- **[docs/DOCKER.md](docs/DOCKER.md)** - Container deployment, Kubernetes examples
- **[docs/ECONOMY-PHASE-2.2.md](docs/ECONOMY-PHASE-2.2.md)** - Economy module design
- **[docs/PHASE-2.3-SUMMARY.md](docs/PHASE-2.3-SUMMARY.md)** - Latest phase summary

---

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
docker build -t theprojec:latest .

# Run interactively
docker run -it --rm -v ${PWD}/data:/app/data theprojec:latest

# Run batch simulation
echo "run 1000 10" | docker run -i --rm -v ${PWD}/data:/app/data theprojec:latest

# Using Docker Compose
docker compose up kernel

# Using Docker Compose (batch mode)
docker compose --profile batch up
```

See [`docs/DOCKER.md`](docs/DOCKER.md) for complete Docker deployment guide.

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
- `stats` - Print detailed statistics (demographics, networks, beliefs, economy, languages)
- `reset [N R k p] [startCondition]` - Reset with optional parameters:
  - `N`: population size (default: 50000)
  - `R`: number of regions (default: 200)
  - `k`: average connections per node (default: 8)
  - `p`: rewiring probability (default: 0.05)
  - `startCondition`: economic starting profile (default: baseline)
- `run T log` - Run T ticks, logging metrics every `log` steps to `metrics.csv`
- `cluster kmeans K` - Detect K cultures using K-means clustering
- `cluster dbscan eps minPts` - Detect cultures using DBSCAN (eps=0.3, minPts=50 recommended)
- `cultures` - Print last detected cultural clusters
- `detect_movements` - Detect political/social movements from last clustering
- `movements` - List all active movements with stats
- `movement ID` - Show detailed info for specific movement ID
- `economy` - Show global economy summary and economic system distribution
- `region R` - Show detailed economy for region R
- `classes` - Show emergent economic classes (wealth decile × sector)
- `quit` - Exit the simulation
- `help` - Show command help

## Example Session

```
# Check initial state
stats

# Run 100 steps
step 100

# Run longer batch with logging
run 1000 10

# Detect cultural clusters
cluster kmeans 5
cultures

# Detect movements
detect_movements
movements

# Check economy
economy
region 42

# Reset with larger population and different economic start
reset 100000 400 10 0.05 prosperity

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

Metrics are written to `metrics.csv` in the working directory:

```csv
gen,welfare,inequality,hardship,polarization_mean,polarization_std,openness,conformity
0,3.485,0.234,0.012,1.1234,0.3012,0.5001,0.4998
10,3.492,0.237,0.014,1.1456,0.3087,0.5003,0.4996
20,3.501,0.241,0.016,1.1678,0.3142,0.5005,0.4994
```

## Architecture

```
core/
├── include/
│   ├── kernel/
│   │   └── Kernel.h          # Core agent-based simulation engine
│   ├── modules/
│   │   ├── Culture.h         # Cultural clustering (K-means, DBSCAN)
│   │   ├── Economy.h         # 5-good trade economy
│   │   ├── Health.h          # Health and mortality
│   │   ├── Movement.h        # Political/social movement formation
│   │   └── Psychology.h      # Psychological state
│   ├── io/
│   │   └── Snapshot.h        # JSON export utilities
│   └── utils/
│       ├── EventLog.h        # Event logging
│       └── helpers.h         # Utility functions
├── src/                       # Implementation files
└── third_party/
    └── httplib.h             # HTTP library (for future server mode)

cli/
├── main_kernel.cpp           # CLI interface
└── http_server*.h/.cpp       # HTTP server (planned)

tests/
├── kernel_tests.cpp          # Unit tests
├── economy_tests.cpp         # Economy tests
└── integration_tests.sh      # Integration tests

data/                         # Output directory
├── metrics.csv               # Simulation metrics
├── snapshots/                # JSON snapshots
├── metrics/                  # Additional metrics
└── logs/                     # Event logs
```

## Integration Points for Modules

The kernel provides clean integration hooks for Phase 2+ modules:

### Agent Fields
```cpp
// Demographics & Identity:
int age;                    // Age in years (incremented by tick)
bool female;                // Gender
int32_t parent_a, parent_b; // Lineage tracking
uint32_t lineage_id;        // Clan/house/family ID
uint8_t primaryLang;        // Primary language (0-3)
double fluency;             // Language fluency (0-1)

// Personality (0-1, mean ~0.5):
double openness;
double conformity;
double assertiveness;
double sociality;

// Module multipliers (written by modules):
double m_comm;              // Communication reach/speed multiplier (tech/media)
double m_susceptibility;    // Influence susceptibility (personality, stress, drugs)
double m_mobility;          // Migration ease (infrastructure, economy)

// Module states:
PsychologicalState psych;   // Stress, trauma, etc.
HealthState health;         // Disease, injury, vitality
```

### Access Methods
```cpp
// Mutable access for modules:
std::vector<Agent>& agentsMut();  // Update multipliers, lineage, language
const std::vector<std::vector<uint32_t>>& regionIndex();  // Spatial queries
Economy& economyMut();            // Update economy
MovementModule& movementsMut();   // Update movements
```

### Example Module Integration (Pseudocode)
```cpp
// Economy module updates susceptibility via hardship
for (auto& agent : kernel.agentsMut()) {
    double hardship = economy.getAgentEconomy(agent.id).hardship;
    agent.m_susceptibility = 0.7 + 0.5 * hardship;
}

// Culture module reads beliefs to compute clustering
std::vector<Cluster> clusters = kMeans.run(kernel);

// Movement module forms movements from cultural clusters
movements.update(kernel, clusters, tick);
```

## Performance

- **50,000 agents**: ~100ms per tick (economy updates every 10 ticks)
- **Memory**: ~15MB for agents + ~2MB for network (8 neighbors/agent)
- **Scalability**: Economy and clustering run at configurable intervals

## Integration with Web Clients

The CLI uses stdin/stdout, making it easy to integrate with:
- Node.js child processes
- WebSocket servers
- React/browser clients
- Batch processing pipelines

**Planned:** HTTP server mode for REST API access (see `cli/http_server*.h`)

## Roadmap

### ✅ Phase 1.0: Basic Kernel (Complete)
- ✅ Agent belief system with personality traits
- ✅ Small-world network topology
- ✅ Deterministic belief updates
- ✅ Language and region systems
- ✅ 50k+ agent capacity
- ✅ Demographics (age, gender, births, deaths)

### ✅ Phase 2.1-2.3: Economy, Culture, Movements (Complete)
- ✅ 5-good trade economy (Food, Energy, Tools, Luxury, Services)
- ✅ Dynamic pricing and regional specialization
- ✅ Economic systems emergence (Market, Planned, Feudal, Cooperative)
- ✅ Cultural clustering (K-means, DBSCAN)
- ✅ Movement formation from cultural clusters
- ✅ Movement lifecycle (Birth, Growth, Plateau, Schism, Decline)
- ✅ Economic class formation (wealth × sector clustering)

### 🔄 Phase 2.4+: Next Steps
- Institutions with bias, legitimacy, rigidity, capacity
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

See [`docs/DESIGN.md`](docs/DESIGN.md) for complete system specifications.
