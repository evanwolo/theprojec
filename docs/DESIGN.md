# Grand Strategy Game Design

A nation-scale grand strategy game where societies evolve bottom-up from millions of agents; high-level structures—cultures, movements, institutions, ideologies, media regimes, tech regimes, and wars—emerge, compete, ossify, collapse, and reform over centuries without scripts.

## Table of Contents

1. [Core Concept](#core-concept)
2. [Kernel (Agent Layer)](#kernel-agent-layer)
3. [Lineage and Kinship](#lineage-and-kinship)
4. [Decision Module](#decision-module-bounded-agency)
5. [Culture and Language](#culture-and-language)
6. [Movements and Ideologies](#movements-and-ideologies)
7. [Institutions and Nations](#institutions-and-nations)
8. [Resource Economy](#resource-economy-macro-layer)
9. [Technology](#technology-modifiers-and-unlocks)
10. [Media](#media-perception-and-narrative)
11. [War Module](#war-module-fronts-logistics-politics)
12. [Diplomacy and Blocs](#diplomacy-and-blocs)
13. [Ontology (Phase Tags)](#ontology-phase-tags)
14. [Player Experience](#player-experience-pure-grand-strategy)
15. [UI and Readability](#ui-and-readability)
16. [Modularity and Implementation](#modularity-and-implementation)

---

## Core Concept

### Vision

A pure grand strategy experience where:
- **Millions of agents** with beliefs, personalities, languages, and lineages form the substrate
- **Emergent structures** (cultures, movements, ideologies, regimes) arise without scripts
- **Strategic governance** through laws, institutions, budgets, tech, media, diplomacy, war
- **Metaphysical phase lens** provides readable patterns and strategic foreshadowing
- **No micromanagement**: no city tiles, no unit stacks, only strategic levers

### Player Role

You govern a nation/region through:
- Laws and policy frameworks
- Institutional design and reform
- Budget allocation across sectors
- Technology priorities
- Media regulation and frameworks
- Diplomatic relations and alliances
- War goals, mobilization, and doctrine

Consequences ripple through social, political, and military domains as agents react, movements form, institutions ossify or reform, and crises emerge or resolve.

---

## Kernel (Agent Layer)

### Agent Structure

Each agent carries:

```cpp
struct Agent {
  uint32_t id;
  BeliefVec beliefs;        // 4D tanh(x): Authority–Liberty, Tradition–Progress, 
                            // Hierarchy–Equality, Faith–Rationalism
  Personality traits;       // openness, conformity, assertiveness, sociality
  uint32_t parent1_id;      // lineage tracking
  uint32_t parent2_id;
  uint32_t lineage_id;      // clan/house/family identifier
  LanguageProfile langs;    // primary + repertoire with fluency levels
  vector<uint32_t> neighbors; // small-world social network
  Region* region;           // geographic location
};
```

### Belief Updates

**Each tick:**
1. For each neighbor within similarity gate (personality-adaptive threshold)
2. Compute distance-weighted influence scaled by:
   - Belief space distance
   - Language communication quality
   - Personality susceptibility (openness, conformity)
   - Global step size (tech multiplier)
3. Apply tanh-bounded update to belief vector
4. Technology adjusts reach (long-range edges) and speed

**Formula:**
```
Δb_i = openness × conformity × Σ_neighbors [ 
  w_ij × language_quality_ij × 
  exp(-(d_ij/threshold)²) × 
  tanh(0.3 × (b_j - b_i))
] × updateSpeed × tech_multiplier
```

### Demographics

- **Birth/Death**: Regional rates produce/remove agents
- **Inheritance**: Children receive:
  - Trait mixture from parents + noise
  - Linguistic repertoire from parents + local exposure
  - Belief priors from parents + local culture mean + noise
  - Lineage ID from dominant parent lineage
- **Migration**: Handled by decision module (bounded utility evaluation)

---

## Lineage and Kinship

### Purpose

Creates continuity, elite networks, status transmission, and emergent class structures.

### Structure

**Per Agent:**
- Parent IDs (mother, father)
- Lineage ID (clan/house identifier)

**Lineage Objects:**
```cpp
struct Lineage {
  uint32_t id;
  double prestige;           // accumulated status
  double wealth_index;       // economic position
  BeliefVec cultural_profile; // mean beliefs of members
  map<Region*, int> presence; // regional distribution
  set<uint32_t> elite_roles;  // institutional positions held
};
```

### Effects

- **Belief transmission**: Stronger weight to parental beliefs during inheritance
- **Informal support**: Kinship network modifies risk/material utility in decisions
- **Status inheritance**: Children of elite lineages start with higher prestige
- **Elite continuity**: Lineage concentration in institutions affects rigidity and legitimacy
- **Coalition formation**: Lineage-based factions within movements and regimes

---

## Decision Module (Bounded Agency)

### Action Set (Per Tick Sample)

Each agent evaluates a small subset of actions:
- **Stay passive**: No action
- **Join/leave movement**: Align with ideological coalition
- **Escalate protest**: Participate in demonstrations/strikes
- **Migrate**: Move to another region
- **Change media diet**: Shift information sources

### Utility Formula

```
U(action) = w_material × material_payoff
          + w_ideological × belief_alignment
          + w_social × (social_payoff + kinship_support)
          - w_risk × (repression_risk + physical_risk)
          - w_effort × effort_cost
```

Weights determined by personality:
- Material: inversely related to ideology/charisma
- Ideological: openness, assertiveness
- Social: sociality, conformity
- Risk: inverse assertiveness, kinship support reduces perceived risk
- Effort: conformity (inertia)

### Decision Process

1. Compute utility for each available action
2. Apply softmax with agent-specific decisiveness (assertiveness)
3. Sample action from probability distribution
4. Update agent state (movement membership, location, media, participation)

### Inputs

- **Economy**: Hardship, inequality, employment, wages
- **Institutions**: Legitimacy, repression level, policy fit
- **Movements**: Platform alignment, power, size, momentum
- **Media**: Perceived legitimacy, risk, movement viability
- **Kinship**: Support network, lineage prestige, elite access
- **Regional**: Prosperity, unrest, connectivity, opportunity

### Outputs

- Movement membership counts
- Protest participation (feeds into movement power)
- Migration flows between regions
- Media consumption distribution
- Electoral preferences (if applicable)

---

## Culture and Language

### Culture Module

**Purpose**: Identify emergent cultural clusters without hard boundaries.

**Method**:
1. Cluster agents in belief space (k-means or DBSCAN variants)
2. Track centroids, coherence (intra-cluster variance), size
3. Compute transmission strength (parent-to-child belief correlation)
4. Identify language composition and regional presence
5. Label cultures when stable over time windows

**Attributes**:
```cpp
struct Culture {
  BeliefVec centroid;
  double coherence;
  double transmission_strength;
  map<Language*, double> language_weights;
  map<Region*, int> regional_presence;
  vector<Lineage*> dominant_lineages;
};
```

**Effects**:
- Cultures form movement bases
- Cultural distance affects diplomacy and war support
- Regime changes that violate dominant culture reduce legitimacy

### Language Module

**Purpose**: Model communication barriers and identity markers.

**Per Agent**:
```cpp
struct LanguageProfile {
  Language* primary;
  map<Language*, double> fluency; // 0..1 for each known language
};
```

**Communication Quality**:
```
quality_ij = min(fluency_i[lang], fluency_j[lang]) × prestige[lang]
```

**Dynamics**:
- **Inheritance**: Children learn parent languages + local dominant language
- **Shift**: Exposure and policy (official language laws) adjust fluency
- **Prestige**: Official status, elite usage, media presence boost prestige
- **Influence**: Belief updates scale by communication quality

**Policy Levers**:
- Designate official languages
- Fund minority language education
- Media language requirements
- Military/bureaucracy language mandates

---

## Movements and Ideologies

### Movement Formation

**Trigger**: Coherent local clusters with charismatic hubs crossing thresholds:
- Size > N agents
- Coherence > threshold
- Charisma density > threshold
- Momentum (growth rate) > threshold

**Structure**:
```cpp
struct Movement {
  BeliefVec platform;              // mean beliefs of members
  map<Culture*, double> bases;     // cultural composition
  map<Class, double> class_bases;  // economic classes
  map<Lineage*, double> lineage_bases;
  map<Region*, double> regional_strength;
  
  double power;                    // institutional + media + street
  double institutional_access;     // seats, ministries, bureaucracy
  double media_presence;
  double street_capacity;          // protest/strike/paramilitary
  
  LifeCycle stage;                 // birth, growth, plateau, schism, decline
  vector<Agent*> leaders;          // high charisma agents
};
```

### Ideology Inference

**Method**:
1. Cluster movement platforms over time windows
2. Identify stable regions of belief space
3. Label ideologies (optionally match to archetype vectors)
4. Track drift, splits, mergers

**Attributes**:
```cpp
struct Ideology {
  string label;                    // e.g., "Liberal Nationalism"
  BeliefVec archetype;             // ideal point (optional)
  vector<Movement*> movements;
  map<Region*, double> regional_variants;
  map<Class, double> class_appeal;
};
```

**Emergence Drivers**:
- **Hardship/inequality**: Radical movements (extremes on Authority, Hierarchy)
- **Prosperity**: Moderate movements or status politics (cultural axes)
- **Personality distributions**: High openness → progressive; high conformity → traditional
- **Geography/connectivity**: Isolated regions form distinct variants
- **Media regime**: Censorship delays but intensifies; freedom accelerates swings
- **Institutional response**: Repression radicalizes; reform co-opts; capture corrupts

### Movement Dynamics

**Growth**: Recruitment from decision module (utility of joining)
**Power**: Aggregates institutional access, media, street capacity
**Schism**: High internal belief variance + leadership conflicts → split
**Decline**: Loss of base, institutional capture, repression, platform obsolescence

---

## Institutions and Nations

### Institution Structure

```cpp
struct Institution {
  string type;                // legislature, executive, military, bureaucracy, media
  BeliefVec bias;             // what it pushes via policy/propaganda
  double legitimacy;          // fit to population beliefs/outputs
  double rigidity;            // resistance to reform (age, centralization, elite concentration)
  double capacity;            // enforcement, administration effectiveness
  
  map<Lineage*, double> elite_composition; // lineage concentration
  Movement* captured_by;      // if captured
  
  double age;                 // time since last major reform
  double centralization;      // vs local autonomy
};
```

### Legitimacy Dynamics

```
legitimacy = correlation(institution_bias, population_mean_beliefs)
           × output_quality (welfare, security, fairness)
           - corruption_perception
           - elite_closure (lineage concentration penalty)
```

### Rigidity and Reform

**Rigidity increases**:
- Age without reform
- Centralization
- Elite lineage concentration
- Capture by stagnant movement

**Rigidity effects**:
- Slower response to crises
- Higher cost of reform
- Increased coup/revolution risk if legitimacy low

**Reform triggers**:
- Player decision (cost scales with rigidity)
- Crisis event (legitimacy < threshold, unrest > threshold)
- Successful movement pressure

**Reform outcomes**:
- Bias shift toward population/movement mean
- Capacity boost (temporary)
- Rigidity reset
- Elite purge (lineage reshuffle)

### Nation Structure

```cpp
struct Nation {
  vector<Region*> regions;
  vector<Institution*> institutions;
  
  Laws laws;                  // media freedom, political system, economic model,
                              // rights, centralization, language policy
  
  double state_capacity;      // aggregate institutional capacity
  double legitimacy;          // weighted mean of institutions
  
  map<Nation*, DiplomaticStatus> relations;
  WarPosture war_posture;
};
```

### Law Effects

Laws are parameter modifiers applied through institutions:
- **Media freedom**: Censorship suppresses visible issues, increases shadow pressure
- **Political system**: Autocracy (fast decisions, low legitimacy feedback) vs democracy (slow, high feedback)
- **Economic model**: Market (high inequality, volatile) vs planned (low inequality, rigid) vs mixed
- **Rights**: Minority protections, labor rights, property rights affect grievances and economy
- **Centralization**: Local autonomy vs central control affects regional legitimacy variance

---

## Resource Economy (Macro Layer)

### Regional Production

```cpp
struct Region {
  Endowments endowments;      // food, energy, industry base
  Technology tech_level;
  Institutions local_institutions;
  
  Production production;      // endowment × tech × efficiency
  Welfare welfare;
  Inequality inequality;
  Hardship hardship;
};

production[resource] = endowment[resource] 
                     × tech_multiplier[resource]
                     × institutional_efficiency
```

### Distribution

**Market model**: Prices clear; high inequality; responsive to shocks  
**Planned model**: Quotas; low inequality; shortage-prone; slow to adapt  
**Mixed model**: Blend of mechanisms

**Output**:
```
welfare = production per capita
inequality = Gini coefficient or variance
hardship = unmet basic needs (food, energy)
```

### Feedback Loops

- **Hardship → grievances**: Low welfare/high hardship increases protest utility
- **Inequality → status politics**: High inequality + high welfare → cultural/identity movements
- **Class formation**: Economic position clusters become movement bases
- **Mobilization costs**: War/strike labor reallocation reduces production
- **War industry**: Reallocate to munitions; generates shortages; affects war support

---

## Technology (Modifiers and Unlocks)

### Tech as Multipliers

```cpp
struct Technology {
  map<Resource, double> production_efficiency;
  double communication_reach;      // belief influence range
  double communication_speed;      // belief update rate
  double repression_effectiveness;
  double repression_risk_reduction;
  double admin_capacity_boost;
  double mobility;                 // migration/logistics throughput
  double infrastructure;           // rail/road/port capacity
};
```

### Diffusion

- Uneven across regions (centers vs periphery)
- Class access varies (elites adopt first)
- Policy affects diffusion (investment, education)

### Strategic Effects

- **Communication tech**: Faster ideological convergence/divergence; long-range influence; media reach
- **Production tech**: Higher welfare; shifts labor distribution; unlocks new resources
- **Repression tech**: Surveillance reduces successful protest; increases shadow pressure if overused
- **Logistics tech**: Longer supply lines in war; faster mobilization; enables large-scale operations
- **Infrastructure tech**: Migration flows; regional integration; reduces regional legitimacy variance

### Unlocks

Certain techs unlock:
- New law options (e.g., social insurance, mass media regulation)
- New war doctrines (maneuver warfare, deep battle, precision strike)
- New diplomatic tools (signals intelligence, cyber operations)

---

## Media (Perception and Narrative)

### Media Outlet Structure

```cpp
struct MediaOutlet {
  double reach;                   // fraction of population exposed
  TechLevel tech;                 // print, broadcast, internet
  BeliefVec bias;                 // editorial slant
  double accuracy;                // vs manipulation/propaganda
  Ownership ownership;            // state, private, movement, foreign
  set<Language*> languages;
  
  double credibility;             // trust level (decays with bias/inaccuracy)
};
```

### Narrative Generation

1. System generates baseline forecasts (legitimacy, hardship, risk, movement power)
2. Media applies bias vector: shift perception toward bias direction
3. Accuracy parameter: high accuracy = small shift; low accuracy = large shift
4. Credibility weights how much agents trust the outlet

**Agent perception**:
```
perceived_legitimacy = weighted_mean(media_narratives, by credibility × language_match)
```

### Media Effects

- **Free media**: Rapid legitimacy feedback; surfaces problems early; accelerates swings
- **Censorship**: Suppresses visible issues; hides casualties/shortages; increases "shadow" pressure (hidden grievances that explode in crises)
- **Propaganda**: Boosts war support, regime legitimacy if credible; backfires if caught lying
- **Foreign media**: Can shift domestic opinion; information warfare tool in diplomacy

### Policy Levers

- **Freedom**: Allow/ban foreign media, opposition media
- **Regulation**: Accuracy standards, bias disclosure
- **Ownership**: State monopoly, private competition, public broadcasting
- **Language**: Mandate languages, fund minority media

---

## War Module (Fronts, Logistics, Politics)

### No Unit Micromanagement

Wars are fought via **fronts** and **theaters** without individual units. Players set strategic parameters; resolution is deterministic but influenced by logistics, doctrine, morale, and politics.

### War Goals and Mobilization

**Player sets**:
- War goals (territory, regime change, reparations, independence)
- Mobilization level (% of population in military)
- Industry allocation (consumer vs munitions)
- Doctrine emphasis (offense/defense, maneuver/attrition, deep battle, remote strike)
- Logistics plan (rail/road/sea routes, depot placement, convoy protection)
- Air/naval tasking (air superiority, interdiction, convoy escort, blockade)
- Media stance (transparency vs censorship, victory claims)

### Front Resolution

**Effective strength per side**:
```
ES = troops × training × equipment × morale × supply × doctrine_multiplier × terrain_multiplier × weather_multiplier
```

**Combat**:
- Side with higher ES has **initiative**: launches attacks more frequently
- Each attack: attacker_ES vs defender_ES → casualties proportional to ratio and doctrine
- Front advances/retreats based on cumulative outcomes
- Breakthrough: if attacker ES >> defender ES, front collapses; deep penetration

**Doctrine effects**:
- **Offense/Defense**: Affects attack/defense multipliers
- **Maneuver**: High mobility, encirclement bonus, supply vulnerability
- **Attrition**: Grind down enemy, slow but steady
- **Deep Battle**: Exploit breakthroughs, requires logistics
- **Remote Strike**: Precision munitions reduce casualties, high tech requirement

### Logistics System

**Supply sources**: Regions with production capacity  
**Supply lines**: Rail, road, sea routes with throughput capacity  
**Depots**: Intermediate storage along lines  
**Fronts**: Consume supply; attrition if undersupplied

**Throughput**:
```
supply_delivered = min(
  production_capacity,
  rail_throughput,
  road_throughput,
  sea_throughput (if applicable),
  depot_capacity
) × (1 - interdiction_loss)
```

**Interdiction**:
- Enemy air strikes on rail/depots
- Naval blockades on sea routes
- Partisan activity in occupied territory
- Sabotage from low legitimacy in rear areas

**Effects of supply shortage**:
- Reduced effective strength
- Increased attrition (starvation, desertion)
- Morale collapse if prolonged

### Morale and War Support

**Front morale**:
```
morale = base_morale 
       + recent_victories
       - recent_defeats
       - casualty_rate
       - supply_shortage
       - ideological_misalignment (if war unpopular among troops)
```

**Home front war support**:
```
war_support = media_narrative_war_support
            + movement_alignment_to_war_goals
            - casualty_sensitivity × total_casualties
            - shortage_impact × (food/energy shortages)
            + ideological_fit (e.g., nationalist movements boost support for unification wars)
```

**Political consequences**:
- **High casualties + low support**: Anti-war movements grow; legitimacy drops; cabinet crises; coups
- **Elite splits**: Lineages/movements in government disagree on war; can trigger regime change
- **War weariness**: Prolonged war reduces morale even if winning; postwar unrest

### Air and Naval Operations

**Air**:
- Air superiority: Required for effective interdiction and ground support
- Interdiction: Reduce enemy supply throughput
- Strategic bombing: Target industry (reduces production) or morale (increases war weariness)

**Naval**:
- Blockade: Cut sea supply lines; reduce trade; starve island/coastal fronts
- Convoy escort: Protect own sea lines
- Amphibious operations: Open new fronts (high cost, high logistics requirement)

### Warscore and Peace

**Warscore components**:
- Objective progress (% of goals achieved)
- Casualty ratio (inflicted vs taken)
- Blockade effectiveness
- Internal unrest (both sides)

**Peace terms** (when warscore threshold reached or negotiated):
- Border changes (regions ceded)
- Reparations (economic transfer)
- Regime concessions (government change, law changes)
- Puppet states (occupation, limited sovereignty)
- Demilitarization (capacity reduction)

### Postwar Dynamics

**Demobilization shock**:
- Veterans return; unemployment spike; hardship
- Veteran movements form (can be stabilizing or destabilizing depending on war outcome)
- Paramilitaries (if regime weak or war lost)

**Regime shifts**:
- Victory: Legitimacy boost; risk of triumphalism/imperialism
- Defeat: Legitimacy collapse; revolution/coup risk; radical movements gain

**Ideological trajectories**:
- Total war reshapes belief space (nationalism, militarism, pacifism)
- Occupation: Occupied populations shift beliefs under foreign institutions/media
- Reconstruction: Aid and institution-building from victors alter trajectories

---

## Diplomacy and Blocs

### Diplomatic Relations

```cpp
enum DiplomaticStatus {
  ALLIANCE,          // mutual defense, coordinate wars
  GUARANTEE,         // protect from aggression
  RIVALRY,           // hostility short of war
  TRADE_PACT,        // economic integration
  FEDERATION,        // shared sovereignty
  PUPPET,            // occupied/controlled
  SANCTION,          // economic punishment
  INFORMATION_WAR    // media/cyber operations
};
```

### Ideological and Cultural Blocs

**Organic formation**:
- Movements with similar platforms across borders align
- Cultural proximity (language, belief centroids) predicts bloc membership
- Institutional similarity (regime type) affects alignment

**Bloc effects**:
- **Military**: Alliances, guarantees, volunteers, lend-lease
- **Economic**: Trade preferences, sanctions coordination
- **Ideological**: Foreign media influence, movement funding, exile hosting

### Gray-Zone Conflict

Tools short of formal war:
- **Covert ops**: Support opposition movements; sabotage institutions
- **Sanctions**: Reduce target economy; harden target population
- **Cyber**: Disrupt logistics, media, admin capacity
- **Proxy war**: Arm movements in civil conflicts

**Escalation ladder**:
Gray-zone → border skirmishes → limited war → total war

**Detection and response**:
- Covert ops have detection risk; if exposed, legitimacy hit and escalation
- Sanctions unify target population if handled well by target regime
- Cyber attribution uncertainty allows deniability but risks miscalculation

---

## Ontology (Phase Tags)

### Metaphysical Layer

A descriptive lens that maps systemic metrics to prime-number phase tags, providing strategic hints and narrative flavor without overriding simulation logic.

### Prime Factorization Tags

**Primes and meanings**:
- **2 (Duality)**: Polarization, binary opposition, us-vs-them
- **3 (Triad/Mediation)**: Third-way synthesis, balance, mediation
- **5 (Proportion)**: Golden ratio, sustainable equilibrium, harmony
- **7 (Completion)**: Cycle end, maturity, order established
- **11 (Crisis)**: Instability, threshold, inflection point
- **13 (Shadow)**: Hidden pressures, suppressed issues, latent conflict

**Composite tags** (examples):
- **2×11 = 22**: Polarized crisis (civil war risk, regime collapse)
- **3×5 = 15**: Mediated balance (stable pluralism)
- **7×13 = 91**: Brittle completion with hidden tension (rigid order masking decay)
- **2^n×7×11**: Extreme polarization in mature system undergoing crisis (revolutionary moment)

### Metric Mapping

**Input metrics** (national or global scope):
```
polarization        → weight toward 2
cluster_count       → if == 3, weight toward 3; if high, dispersion
legitimacy × rigidity → high both → 7 (completion/order)
hardship + inequality → weight toward 11 (crisis)
media_divergence    → weight toward 2 (duality) or 13 (shadow if censored)
crisis_frequency    → weight toward 11
institutional_age   → weight toward 7 or 13 (ossification)
```

**Composite integer**:
```
ontology_value = product of weighted primes
phase_tag = prime_factorization(ontology_value)
```

### UI Presentation

**Ontology strip**:
Shows current phase tag with tooltip explanation:
- "Duality-Crisis (2×11): Polarized society approaching threshold. Expect political breakdown or regime shift."
- "Proportion (5): Balanced equilibrium. Stable period; good time for long-term reforms."
- "Completion-Shadow (7×13): Order appears stable but hidden tensions accumulate. Monitor suppressed movements and media credibility."

**Event hints**:
Event options include phase tag annotations:
- [Reform] "Legitimacy boost, shifts toward 3 (mediation)"
- [Repress] "Short-term order (7) but increases shadow (13)"

### Limitations

- **Descriptive, not prescriptive**: Tags summarize state; they don't drive mechanics
- **Fuzzy boundaries**: Metric thresholds are tuned for readability, not precision
- **Narrative aid**: Helps players and AI narrators frame the "feel" of an era

---

## Player Experience (Pure Grand Strategy)

### Core Loop

1. **Observe**: Check overlays (culture, movements, economy, media, fronts, phases)
2. **Diagnose**: Read tooltips, metrics, event triggers to understand causal chains
3. **Decide**: Choose laws, budgets, tech focus, diplomatic moves, war posture
4. **Steer**: Watch emergent consequences over ticks/months/years
5. **React**: Handle crises, reform institutions, negotiate peace, manage coalitions

### Strategic Levers

**Laws and institutions**:
- Reform authoritarian system → increase media freedom, democratic institutions
- Risk: Legitimacy dip during transition; movements demand more
- Reward: Long-term stability if aligned with population

**Economy and welfare**:
- Invest in production, redistribute wealth, manage inequality
- Risk: High taxes reduce growth; low taxes increase inequality/unrest
- Reward: High welfare = legitimacy buffer

**Tech and communication**:
- Prioritize communication tech → faster ideological convergence, media reach
- Prioritize production tech → welfare boost, economic power
- Prioritize military tech → logistics, repression, doctrine unlocks

**Media and narrative**:
- Free media: Rapid feedback, early crisis signals, volatile legitimacy
- Censorship: Slow feedback, hidden crises, explosive unrest when legitimacy breaks

**Diplomacy**:
- Build alliances for security, trade for economy, blocs for ideological solidarity
- Risk: Entangling commitments, culture clash with allies
- Reward: Shared defense, economic growth, ideological momentum

**War**:
- Set mobilization, doctrine, logistics priorities
- Balance war support (media, casualties, shortages) with military effectiveness
- Risk: Overextension, legitimacy collapse, coup, revolution
- Reward: Territory, security, regime change in rivals

### Victory Conditions (Open-Ended)

**Formal types**:
- **Stability/Prosperity**: Maintain high legitimacy and welfare for X years
- **Ideological Dominance**: Spread ideology to X% of global population
- **Tech Leadership**: Reach top tier in all tech categories
- **Resilience**: Survive Y crises and transitions without collapse

**Player-defined goals**:
- Reform rigid autocracy into balanced democracy without civil war
- Unify fragmented culture across borders
- Survive between hostile blocs as neutral buffer
- Integrate shadow pressures peacefully before they explode
- Build multi-cultural federation with high legitimacy

### Event Windows

**Trigger conditions** (examples):
- Legitimacy < 40% + hardship > 60% → "Popular Unrest"
- Movement power > 30% + institutional capture → "Coalition Crisis"
- War support < 30% + casualties > 100k → "War Weariness"
- Phase tag = 2×11 → "Polarized Crisis"

**Event structure**:
- **Context**: Why this happened (causal chain from metrics)
- **Options** (2–4 choices):
  - [Reform] Immediate cost, long-term legitimacy boost, shifts metrics toward 3 or 5
  - [Repress] Short-term stability, increases rigidity and shadow pressure
  - [Co-opt] Buy off movements, increases state capacity, reduces movement power
  - [Wait] Let it play out; risky but no immediate cost
- **Phase hints**: What each option does to ontology tags
- **Consequences**: Visible short-term modifiers + emergent long-term effects

---

## UI and Readability

### World Map Interface

**Central view**: 3D globe or flat map with nation borders and regions

**Overlay modes**:
- **Culture**: Color by dominant culture; shading for coherence
- **Belief axes**: Heatmaps for each dimension (Authority, Tradition, Hierarchy, Faith)
- **Languages**: Primary language distribution; multilingual borders
- **Movements**: Size and strength by region; platform visualization
- **Unrest**: Protest activity, strike risk, paramilitary presence
- **Prosperity**: Welfare and inequality gradients
- **Technology**: Tech level disparities across regions
- **Media**: Outlet reach, bias, credibility maps
- **Fronts**: Active war zones, supply lines, front strength
- **Phase tags**: Color by dominant phase (Duality=red, Mediation=blue, Crisis=orange, etc.)

### Side Panels

**Nation panel**:
- Laws and institutions
- Legitimacy, state capacity, rigidity
- Budget allocation
- Tech tree progress

**Interest groups / Movements**:
- List of active movements with power bars
- Platform summaries (belief vectors)
- Bases (cultures, classes, lineages, regions)
- Demands and alignment to government

**Economy dashboard**:
- Production by resource and region
- Welfare, inequality, hardship indicators
- Trade flows and sanctions

**Media landscape**:
- Outlet list with reach, bias, credibility
- Narrative trends (what media is pushing)
- Censorship impact (shadow pressure gauge)

**Diplomacy panel**:
- Relations with all nations
- Bloc memberships
- Active wars and peace negotiations
- Covert ops and gray-zone activities

**War panel** (during war):
- Front map with strength indicators
- Logistics view (supply lines, throughput, interdiction)
- Mobilization level and war support
- Warscore and peace term options

**Ontology strip** (bottom or side):
- Current phase tag with icon
- Tooltip: "What this means and what to watch for"
- Historical phase transitions (timeline)

### Tooltips and Causality Chains

**Example tooltip** (hovering over legitimacy drop):
```
Legitimacy: 42% (↓ 15%)
Causes:
  - Rising hardship in industrial belt (−8%)
    ↳ Production disruption from strikes
    ↳ Inequality spike from war mobilization
  - Media credibility loss (−5%)
    ↳ Censorship of casualty reports
    ↳ Propaganda backfire detected
  - Institutional rigidity (−2%)
    ↳ Elite lineage concentration in cabinet

Effects:
  - Movement recruitment +20%
  - Strike risk +30%
  - Coup risk +10% (if continues 6 months)
```

### Readability Enhancements

- **Color coding**: Green=stable, yellow=watch, orange=crisis, red=collapse
- **Icons**: Visual symbols for phase tags, movements, tech unlocks
- **Animations**: Slow-motion replays of front collapses, movement surges, legitimacy crashes
- **Narrative summaries**: AI-generated short text snippets for major events

---

## Modularity and Implementation

### System Separation

Each major system is a module with clear interfaces:

```
Kernel (agents, beliefs, network, demographics)
  ↓ provides agent state
Culture (clustering, coherence, transmission)
  ↓ provides cultural identities
Language (repertoires, communication quality)
  ↓ provides influence scaling
Lineage (kinship, prestige, elite tracking)
  ↓ provides support networks and elite concentration
Decision (bounded utility, action sampling)
  ↓ provides movement membership, migration, participation
Movements (formation, platforms, power, lifecycle)
  ↓ provides political forces
Institutions (bias, legitimacy, rigidity, capacity)
  ↓ provides policy effects and regime stability
Economy (production, distribution, welfare, inequality)
  ↓ provides material conditions
Technology (multipliers, unlocks)
  ↓ provides parameter modifiers
Media (outlets, bias, narratives, credibility)
  ↓ provides perception layer
War (fronts, logistics, morale, politics)
  ↓ provides military outcomes
Diplomacy (relations, blocs, gray-zone)
  ↓ provides external pressures
Ontology (metrics → phase tags)
  ↓ provides strategic hints and narrative framing
```

### Data Exchange

**Structured data formats**:
- Agent state: belief vectors, traits, lineage IDs, languages, neighbors, region
- Culture state: centroids, coherence, sizes, languages, lineages
- Movement state: platforms, bases, power components, leaders
- Institution state: bias, legitimacy, rigidity, capacity, elite composition
- Economy state: production, welfare, inequality, hardship per region
- Tech state: multipliers, unlocks
- Media state: outlets with reach, bias, credibility
- War state: fronts with ES, supply, morale; warscore; peace terms
- Ontology state: metric values, phase tags, interpretations

### Incremental Development

**Phase 1** (Current):
- ✅ Kernel operational (agents, beliefs, network, updates)
- 🔄 Metrics and clustering for cultures and movements

**Phase 2**:
- Add lineage (parent IDs, lineage objects)
- Implement decision module
- Build institutions, economy, tech, media, language modules
- Validate emergent politics (movements form, legitimacy fluctuates)

**Phase 3**:
- Event system with metric triggers
- War module (fronts, logistics, war support)
- Diplomacy (alliances, sanctions, gray-zone)
- Full UI panels for control and visibility

**Phase 4**:
- Ontology tagging system
- Batch simulation runs for tuning
- "Story probes": seed scenarios, check for emergent narratives
- Performance optimization (vectorization, parallelization)
- Telemetry for player actions and outcomes

**Phase 5**:
- Optional AI labeling (GPT-style naming of ideologies, eras, regimes)
- Narrative summary generation from system state
- Modding API: scenario files, archetype vectors, law templates
- Steam workshop integration
- User-generated content showcase

### Testing and Validation

**Unit tests**:
- Each module independently tested with synthetic inputs

**Integration tests**:
- Kernel + culture: clusters form from belief space
- Decision + movements: agents join movements based on utility
- Economy + institutions: welfare affects legitimacy
- War + media: casualties affect war support

**Emergence validation** ("story probes"):
- Seed: Stable autocracy with high inequality
  - Expected: Radical movements form, demand reform
- Seed: Multicultural federation with weak institutions
  - Expected: Separatist movements, legitimacy variance
- Seed: Two rival nations with similar ideologies
  - Expected: Diplomatic tension, gray-zone conflict, possible war
- Seed: High-tech democracy with free media
  - Expected: Rapid legitimacy swings, responsive governance

**Performance targets**:
- 100k agents: <100ms per tick
- 1M agents: <1s per tick (with optimizations)
- Parallel processing: culture clustering, decision sampling, front resolution

---

## External Validation & Enhancement Roadmap (November 2025)

### Review Summary
This design has received comprehensive validation as an ambitious fusion of agent-based modeling and strategic gameplay, praised for its organic emergence, technical rigor, and Docker scalability. The 4D belief space, personality-driven dynamics, and abstraction layers (no unit micromanagement) were highlighted as particular strengths, with the ontology phase tags providing strategic readability.

### Key Strengths Validated
- **Organic Emergence**: Bottom-up formation of cultures, movements, and institutions without scripts
- **Strategic Depth**: Meaningful player levers (laws, budgets, media) that ripple through agent populations
- **Technical Excellence**: Production-grade C++ kernel with Docker deployment and performance optimizations
- **Metaphysical Lens**: Phase tags and overlays make complex dynamics readable and strategic

### Planned Enhancements

#### Performance Scaling (Phase 2.4)
- **Million-Agent Support**: Thread parallelization and GPU offload for belief updates and clustering
- **Spatial Optimization**: Quad-tree partitioning for O(N) neighbor computations
- **Vectorization**: Extended AVX-512 optimizations for agent processing

#### Decision Module Deepening (Phase 2.5)
- **Contextual Actions**: Adaptive action sets (war-time mobilization vs peace-time diplomacy)
- **Strategic Ripples**: Chain reactions from decisions creating deeper gameplay consequences
- **Crisis Response**: Dynamic utility tuning based on threat levels

#### Analytics & Validation (Phase 2.6)
- **Quantitative Metrics**: Movement lifecycles, legitimacy correlations, crisis frequencies
- **MATLAB Integration**: Post-simulation pattern analysis pipelines
- **AI Archetypes**: Tiny LLM integration for automated ideology labeling

#### Future Vision (Phase 3.0+)
- **Multiplayer Federation**: Distributed simulation nodes with agent migration
- **LLM Ideologies**: AI-generated belief systems for diverse starting conditions
- **Narrative Automation**: Automated story generation from emergent patterns

These enhancements will scale the simulation to millions of agents while maintaining the core philosophy of emergent, script-free complexity.

---
