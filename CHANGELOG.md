# Changelog

## Phase 2.3 - Economy Bootstrap & Integration (November 2025)

### Fixes
- **Bootstrap:** 2.0x food endowment boost, 30% lower subsistence requirements
- **Validation:** 5000-tick simulations show stable mean welfare (3.485) and regional specialization
- **Performance:** <100ms/tick with economy updates every 10 ticks

### Features
- **Economic feedback:** Hardship increases belief susceptibility (0.05 → 0.15 at max hardship)
- **Identity groups:** Wealth decile + sector clustering for emergent class formation
- **Inequality tracking:** Gini coefficient, top 10% share, bottom 50% share

### Results
- Regional specialization emerges over 3000+ ticks
- Economic systems remain stable across authority/tradition space
- Transport costs (2%/hop) create natural trade zones

---

## Phase 2.2 - Economy Module (October 2025)

### Features
- **5-good trade economy:** Food, Energy, Tools, Luxury, Services
- **Agent-level tracking:** wealth, income, productivity, sector, hardship
- **Economic systems:** Market, Planned, Feudal, Cooperative (belief-dependent)
- **Dynamic pricing:** ±5%/tick supply/demand adjustment, bounded 0.1–10×
- **Regional specialization:** Comparative advantage with +100% peak production
- **Trade network:** Transport costs (2%/hop), surplus→deficit only
- **Class formation:** Wealth concentration enables movement bases

### Implementation
- Economy module integrated into Kernel (updates every 10 ticks)
- CLI commands: `economy status`, `economy agents`, `economy trade`
- JSON export includes economic metrics

---

## Phase 2.1 - Clustering & Culture (September 2025)

### Features
- **Charismatic hub detection:** High-assertiveness agents with large influence
- **Cultural clustering:** Modularity optimization for belief communities
- **Identity groups:** Region + language + wealth decile
- **Cross-cultural interaction:** Language attenuation (0.7^distance)

---

## Phase 1 v0.2 - Core Kernel (August 2025)

### Features
- **50,000 agents:** 4D belief space (Authority, Tradition, Hierarchy, Faith)
- **Watts-Strogatz networks:** Small-world with configurable rewiring (0.05)
- **Personality traits:** Openness, conformity, assertiveness, sociality
- **Region system:** 200 regions for spatial heterogeneity
- **Belief dynamics:** Neighbor influence, conformity pressure, opinion leaders
- **Fast tanh:** ~3.5x speedup for activation functions

### Architecture
- Modular design: Kernel + Economy + Culture + Health + Psychology
- CMake build system with GoogleTest integration
- Docker multi-stage build (<150MB final image)

---

## Phase 1 v0.1 - Original Prototype (Legacy)

### Features
- 200-agent proof-of-concept
- Basic opinion dynamics with network rewiring
- Snapshot export to JSON

**Note:** Archived in `legacy/` directory. Use Phase 2+ codebase.
