#include "modules/Economy.h"
#include "modules/TradeNetwork.h"
#include "kernel/Kernel.h"  // For Agent definition
#include <algorithm>
#include <numeric>
#include <cmath>
#include <random>
#include <cctype>
#include <iostream>
#include <memory>
#include <functional>
#include <thread>
#include <atomic>

// Thread-local RNG for parallel operations (prevents race conditions)
// Uses a combination of random_device + thread ID + global counter for unique seeding
namespace {
    std::atomic<uint64_t> global_seed_counter_econ{0};
    
    uint64_t generateThreadSeedEcon() {
        std::random_device rd;
        uint64_t base = rd();
        uint64_t thread_component = static_cast<uint64_t>(
            std::hash<std::thread::id>{}(std::this_thread::get_id()));
        uint64_t counter = global_seed_counter_econ.fetch_add(1, std::memory_order_relaxed);
        return base ^ (thread_component * 0x9e3779b97f4a7c15ULL) ^ (counter * 0xbf58476d1ce4e5b9ULL);
    }
    
    thread_local std::mt19937_64 tl_rng{generateThreadSeedEcon()};
    
    std::mt19937_64& getThreadLocalRNG() {
        return tl_rng;
    }
}

// EMERGENT SUBSISTENCE: Base values that regions modify based on climate/culture/development
// These are STARTING POINTS, not universal truths
constexpr double BASE_FOOD_SUBSISTENCE = 0.7;      // varies by climate: cold regions need more
constexpr double BASE_ENERGY_SUBSISTENCE = 0.35;   // varies by latitude: cold/hot extremes need more
constexpr double BASE_TOOLS_SUBSISTENCE = 0.2;     // varies by development: industrial regions need more
constexpr double BASE_LUXURY_SUBSISTENCE = 0.0;    // varies by culture: status-driven cultures need more
constexpr double BASE_SERVICES_SUBSISTENCE = 0.15; // varies by urbanization: cities need more

// Regional subsistence modifiers (computed per-region based on geography)
struct RegionalNeeds {
    double food = BASE_FOOD_SUBSISTENCE;
    double energy = BASE_ENERGY_SUBSISTENCE;
    double tools = BASE_TOOLS_SUBSISTENCE;
    double luxury = BASE_LUXURY_SUBSISTENCE;
    double services = BASE_SERVICES_SUBSISTENCE;
};

// Compute regional needs based on geography and development
RegionalNeeds computeRegionalNeeds(double x, double y, double development, double population_density) {
    RegionalNeeds needs;
    
    // Climate proxy: y-coordinate (0=south/warm, 1=north/cold)
    double climate_factor = std::abs(y - 0.5) * 2.0;  // 0 at equator, 1 at poles
    
    // Food needs: cold climates need more calories
    needs.food = BASE_FOOD_SUBSISTENCE * (1.0 + climate_factor * 0.3);
    
    // Energy needs: extreme climates (hot or cold) need more
    double latitude_extreme = std::abs(y - 0.5) * 2.0;
    needs.energy = BASE_ENERGY_SUBSISTENCE * (1.0 + latitude_extreme * 0.5);
    
    // Tools needs: developed regions have higher tool dependency
    needs.tools = BASE_TOOLS_SUBSISTENCE * (0.8 + development * 0.4);
    
    // Luxury needs: emerges with development and urbanization
    needs.luxury = BASE_LUXURY_SUBSISTENCE + development * 0.15 + population_density * 0.05;
    
    // Services needs: urban areas need more services
    needs.services = BASE_SERVICES_SUBSISTENCE * (0.7 + population_density * 0.6);
    
    return needs;
}

// Development rates
constexpr double DEVELOPMENT_GROWTH_RATE = 0.01;  // per tick with surplus
constexpr double DEVELOPMENT_DECAY_RATE = 0.005;  // per tick with hardship

// Specialization evolution rate
constexpr double SPECIALIZATION_RATE = 0.001;  // per tick

// Price adjustment rate
constexpr double PRICE_ADJUSTMENT_RATE = 0.05;  // per tick based on supply/demand

// Transport cost (scales with distance)
constexpr double BASE_TRANSPORT_COST = 0.02;  // 2% per hop

Economy::~Economy() = default;

void Economy::init(std::uint32_t num_regions,
                   std::uint32_t num_agents,
                   std::mt19937_64& rng,
                   const std::string& start_condition) {
    regions_.clear();
    regions_.reserve(num_regions);
    trade_links_.clear();
    agents_.clear();
    start_condition_name_ = start_condition;
    start_profile_ = resolveStartCondition(start_condition);
    
    // Initialize trade network
    trade_network_ = std::make_unique<TradeNetwork>();
    trade_network_->configure(num_regions);
    
    std::normal_distribution<double> devNoise(0.0, start_profile_.developmentJitter);
    
    // Arrange regions in a grid for geographic calculations
    // e.g., 200 regions → 14x14 grid (196) + 4 extra
    std::uint32_t gridSize = static_cast<std::uint32_t>(std::ceil(std::sqrt(num_regions)));
    std::uniform_real_distribution<double> jitter(-0.3, 0.3);  // position jitter within cell
    
    for (std::uint32_t i = 0; i < num_regions; ++i) {
        RegionalEconomy region;
        region.region_id = i;
        
        // Place in grid with slight randomization
        std::uint32_t gridX = i % gridSize;
        std::uint32_t gridY = i / gridSize;
        region.x = (gridX + 0.5 + jitter(rng) * 0.5) / gridSize;  // normalize to [0,1]
        region.y = (gridY + 0.5 + jitter(rng) * 0.5) / gridSize;
        region.x = std::clamp(region.x, 0.0, 1.0);
        region.y = std::clamp(region.y, 0.0, 1.0);
        
        double devSample = start_profile_.baseDevelopment + devNoise(rng);
        region.development = std::clamp(devSample, 0.02, 5.0);
        region.economic_system = start_profile_.defaultSystem;  // Initial system
        region.system_stability = 1.0;
        
        regions_.push_back(region);
    }
    
    initializeEndowments(rng);
    initializeTradeNetwork();
    initializeAgents(num_agents, rng);
}

void Economy::update(const std::vector<std::uint32_t>& region_populations,
                    const std::vector<std::array<double, 4>>& region_belief_centroids,
                    const std::vector<Agent>& agents,
                    std::uint64_t generation,
                    const std::vector<std::vector<std::uint32_t>>* region_index) {
    // Update population counts
    for (std::size_t i = 0; i < regions_.size(); ++i) {
        regions_[i].population = region_populations[i];
    }
    
    // Economic evolution happens gradually
    if (generation % 10 == 0) {
        evolveSpecialization();
        evolveDevelopment();
        // Use dominant pole analysis when we have per-agent data
        if (region_index != nullptr && !agents.empty()) {
            evolveEconomicSystems(agents, *region_index);
        } else {
            // Fallback to mean-based analysis (legacy)
            evolveEconomicSystems(region_belief_centroids);
        }
    }
    
    computeProduction();
    computeTrade();
    computeConsumption();
    updatePrices();
    distributeIncome(agents, region_index);
    computeWelfare();
    computeInequality(agents, region_index);
    computeHardship();
}

void Economy::computeProduction() {
    for (auto& region : regions_) {
        for (int g = 0; g < kGoodTypes; ++g) {
            // production = endowment_per_capita × population × specialization × tech × efficiency × development × (1 - war)
            double spec_bonus = 1.0 + region.specialization[g];
            double dev_bonus = 1.0 + region.development * 0.2;
            
            region.production[g] = region.endowments[g] 
                                 * region.population
                                 * spec_bonus
                                 * region.tech_multipliers[g]
                                 * region.efficiency
                                 * dev_bonus
                                 * (1.0 - war_allocation_);
        }
    }
}

void Economy::computeWelfare() {
    for (auto& region : regions_) {
        if (region.population == 0) {
            region.welfare = 1.0;
            continue;
        }
        
        // Welfare = weighted average consumption per capita
        // Essentials weighted more heavily
        double essential_consumption = 
            region.consumption[FOOD] * 2.0 +
            region.consumption[ENERGY] * 1.5 +
            region.consumption[TOOLS] * 1.0 +
            region.consumption[SERVICES] * 1.2;
        
        double luxury_consumption = region.consumption[LUXURY] * 0.5;
        
        double total_weighted = essential_consumption + luxury_consumption;
        double weight_sum = 2.0 + 1.5 + 1.0 + 1.2 + 0.5;  // 6.2
        
        region.welfare = (total_weighted / weight_sum) / region.population;
    }
}

void Economy::computeInequality(const std::vector<Agent>& agents,
                                const std::vector<std::vector<std::uint32_t>>* region_index) {
    // Compute Gini coefficient for each region based on agent wealth
    // This is now FULLY EMERGENT - no overrides based on economic system labels
    for (std::size_t i = 0; i < regions_.size(); ++i) {
        if (regions_[i].population == 0) {
            regions_[i].inequality = 0.0;
            continue;
        }
        
        // Compute Gini from agent wealth distribution - this is the TRUE inequality
        // Use region_index if available for faster lookup
        double gini = 0.0;
        if (region_index && i < region_index->size()) {
            const auto& agent_ids = (*region_index)[i];
            if (agent_ids.empty()) {
                gini = 0.0;
            } else {
                // Fast Gini using pre-indexed agents
                std::vector<double> wealths;
                wealths.reserve(agent_ids.size());
                for (auto aid : agent_ids) {
                    if (aid < agents_.size()) {
                        wealths.push_back(agents_[aid].wealth);
                    }
                }
                
                if (wealths.size() < 2) {
                    gini = 0.0;
                } else {
                    std::sort(wealths.begin(), wealths.end());
                    double sum_of_differences = 0.0;
                    double total = 0.0;
                    std::size_t n = wealths.size();
                    for (std::size_t j = 0; j < n; ++j) {
                        total += wealths[j];
                        sum_of_differences += wealths[j] * (2.0 * j - n + 1.0);
                    }
                    gini = (total > 0.0) ? sum_of_differences / (n * total) : 0.0;
                }
            }
        } else {
            gini = computeRegionGini(static_cast<std::uint32_t>(i), agents);
        }
        
        // No more overrides! The economic system's inequality emerges from
        // actual agent wealth distribution, not predetermined assumptions
        regions_[i].inequality = gini;
    }
}

void Economy::computeHardship() {
    for (auto& region : regions_) {
        if (region.population == 0) {
            region.hardship = 0.0;
            continue;
        }
        
        // Hardship = fraction of basic needs unmet (using consumption, not production)
        double food_per_capita = region.consumption[FOOD] / region.population;
        double energy_per_capita = region.consumption[ENERGY] / region.population;
        double tools_per_capita = region.consumption[TOOLS] / region.population;
        double services_per_capita = region.consumption[SERVICES] / region.population;
        
        // EMERGENT NEEDS: Regional subsistence varies by geography and development
        double pop_density = region.population / 500.0;  // normalized density
        RegionalNeeds needs = computeRegionalNeeds(region.x, region.y, region.development, pop_density);
        
        double food_deficit = std::max(0.0, needs.food - food_per_capita) / needs.food;
        double energy_deficit = std::max(0.0, needs.energy - energy_per_capita) / needs.energy;
        double tools_deficit = std::max(0.0, needs.tools - tools_per_capita) / std::max(0.01, needs.tools);
        double services_deficit = std::max(0.0, needs.services - services_per_capita) / std::max(0.01, needs.services);
        
        // EMERGENT WEIGHTS: Priorities vary by development level
        // Undeveloped regions: food is critical (survival focus)
        // Developed regions: services/tools matter more (quality of life focus)
        double food_weight = 0.5 - region.development * 0.15;      // 0.50 → 0.35
        double energy_weight = 0.3 - region.development * 0.05;    // 0.30 → 0.25  
        double tools_weight = 0.1 + region.development * 0.10;     // 0.10 → 0.20
        double services_weight = 0.1 + region.development * 0.10;  // 0.10 → 0.20
        
        region.hardship = (food_deficit * food_weight + energy_deficit * energy_weight + 
                          tools_deficit * tools_weight + services_deficit * services_weight);
        region.hardship = std::max(0.0, std::min(1.0, region.hardship));
    }
}

void Economy::initializeEndowments(std::mt19937_64& rng) {
    // Create DRAMATIC geographic variation - regions are SPECIALIZED, not self-sufficient
    // This creates scarcity, trade necessity, and economic interdependence
    // Future: tie to actual geography/biomes when terrain is added
    
    std::uniform_real_distribution<double> base_dist(0.0, 1.0);
    std::uniform_int_distribution<int> specialization_choice(0, kGoodTypes - 1);
    
    // Create "resource zones" - clusters of regions with similar endowments
    // This simulates geographic features like fertile plains, mineral deposits, forests, etc.
    std::vector<int> primary_resource(regions_.size());
    std::vector<int> secondary_resource(regions_.size());
    
    for (std::size_t i = 0; i < regions_.size(); ++i) {
        // Primary specialization (what this region is GOOD at)
        primary_resource[i] = specialization_choice(rng);
        
        // Secondary resource (mediocre at this)
        do {
            secondary_resource[i] = specialization_choice(rng);
        } while (secondary_resource[i] == primary_resource[i]);
    }
    
    for (std::size_t i = 0; i < regions_.size(); ++i) {
        auto& region = regions_[i];
        
        // Start with VERY LOW base endowments (scarcity)
        for (int g = 0; g < kGoodTypes; ++g) {
            region.endowments[g] = 0.2 + base_dist(rng) * 0.2;  // 0.2-0.4 per capita (SCARCE)
        }
        
        // PRIMARY resource: ABUNDANT (3-5x base)
        int primary = primary_resource[i];
        region.endowments[primary] = 2.0 + base_dist(rng) * 2.0;  // 2.0-4.0 per capita
        
        // SECONDARY resource: ADEQUATE (1.5-2x base)
        int secondary = secondary_resource[i];
        region.endowments[secondary] = 0.8 + base_dist(rng) * 0.8;  // 0.8-1.6 per capita
        
        // Add geographic clustering: nearby regions have correlated resources
        // (simulates mountain ranges, river valleys, coastal areas, etc.)
        if (i > 0 && base_dist(rng) < 0.3) {  // 30% chance to inherit neighbor's abundance
            const auto& neighbor = regions_[i - 1];
            for (int g = 0; g < kGoodTypes; ++g) {
                if (neighbor.endowments[g] > 1.5) {  // neighbor is rich in this
                    region.endowments[g] = std::max(region.endowments[g], 
                                                    neighbor.endowments[g] * (0.6 + base_dist(rng) * 0.3));
                }
            }
        }
        
        // Some regions are DESPERATELY POOR in certain goods (0.05-0.15 per capita)
        // This creates absolute scarcity and trade dependency
        int num_scarce = std::uniform_int_distribution<int>(1, 2)(rng);
        for (int s = 0; s < num_scarce; ++s) {
            int scarce_good;
            do {
                scarce_good = specialization_choice(rng);
            } while (scarce_good == primary || scarce_good == secondary);
            
            region.endowments[scarce_good] = 0.05 + base_dist(rng) * 0.10;  // DESPERATE scarcity
        }
        
        // Initialize specialization at zero (will evolve based on trade patterns)
        for (int g = 0; g < kGoodTypes; ++g) {
            region.specialization[g] = 0.0;
        }

        for (int g = 0; g < kGoodTypes; ++g) {
            region.endowments[g] *= start_profile_.endowmentMultipliers[g];
        }
    }
}

void Economy::initializeTradeNetwork() {
    // EMERGENT TRADE NETWORK: Partner count varies by geographic position and development
    // Coastal/central regions naturally have more partners than isolated ones
    std::vector<std::vector<std::uint32_t>> trade_partners(regions_.size());
    
    // Grid dimensions for geographic distance
    int grid_size = static_cast<int>(std::ceil(std::sqrt(regions_.size())));
    
    for (std::size_t i = 0; i < regions_.size(); ++i) {
        regions_[i].trade_partners.clear();
        
        // Position in conceptual grid
        int row_i = static_cast<int>(i) / grid_size;
        int col_i = static_cast<int>(i) % grid_size;
        double centrality = 1.0 - (std::abs(row_i - grid_size/2.0) + std::abs(col_i - grid_size/2.0)) / grid_size;
        
        // Central regions have more trade connections (2-15 based on position)
        int base_partners = 2 + static_cast<int>(centrality * 8);
        int partner_variance = static_cast<int>(regions_[i].development * 5); // developed regions trade more
        int max_partners = std::min(base_partners + partner_variance, static_cast<int>(regions_.size()) - 1);
        
        // Find partners by geographic proximity (not fixed offset)
        std::vector<std::pair<double, std::uint32_t>> distance_to_region;
        for (std::size_t j = 0; j < regions_.size(); ++j) {
            if (i == j) continue;
            int row_j = static_cast<int>(j) / grid_size;
            int col_j = static_cast<int>(j) % grid_size;
            double dist = std::sqrt((row_i - row_j) * (row_i - row_j) + (col_i - col_j) * (col_i - col_j));
            // Add some randomness to distance (simulates terrain/historical routes)
            dist *= (0.8 + regions_[j].endowments[0] * 0.4); // resource-rich regions are "closer"
            distance_to_region.push_back({dist, static_cast<std::uint32_t>(j)});
        }
        
        // Sort by effective distance and take nearest partners
        std::sort(distance_to_region.begin(), distance_to_region.end());
        for (int k = 0; k < max_partners && k < static_cast<int>(distance_to_region.size()); ++k) {
            regions_[i].trade_partners.push_back(distance_to_region[k].second);
            trade_partners[i].push_back(distance_to_region[k].second);
        }
    }
    
    // Build matrix topology
    if (trade_network_) {
        trade_network_->buildTopology(trade_partners);
    }
}

void Economy::initializeAgents(std::uint32_t num_agents, std::mt19937_64& rng) {
    agents_.clear();
    agents_.reserve(num_agents);
    
    // Log-normal distribution for wealth
    std::lognormal_distribution<double> wealth_dist(start_profile_.wealthLogMean,
                                                   start_profile_.wealthLogStd);
    std::uniform_int_distribution<int> sector_dist(0, kGoodTypes - 1);
    // Individual productivity variance (skills, education, health)
    std::normal_distribution<double> productivity_dist(start_profile_.productivityMean,
                                                       start_profile_.productivityStd);
    
    for (std::uint32_t i = 0; i < num_agents; ++i) {
        AgentEconomy agent;
        agent.wealth = std::max(0.05, wealth_dist(rng));
        agent.income = 1.0;
        agent.productivity = std::clamp(productivity_dist(rng), 0.2, 3.0);
        agent.sector = sector_dist(rng);
        agent.hardship = 0.0;
        
        agents_.push_back(agent);
    }
}

void Economy::evolveSpecialization() {
    // Regions gradually specialize based on comparative advantage
    for (auto& region : regions_) {
        // Find resource with highest endowment
        int best_good = 0;
        double best_endowment = region.endowments[0];
        
        for (int g = 1; g < kGoodTypes; ++g) {
            if (region.endowments[g] > best_endowment) {
                best_endowment = region.endowments[g];
                best_good = g;
            }
        }
        
        // Increase specialization in best resource, decrease in others
        for (int g = 0; g < kGoodTypes; ++g) {
            if (g == best_good) {
                region.specialization[g] += SPECIALIZATION_RATE;
                region.specialization[g] = std::min(2.0, region.specialization[g]);
            } else {
                region.specialization[g] -= SPECIALIZATION_RATE * 0.5;
                region.specialization[g] = std::max(-0.5, region.specialization[g]);
            }
        }
    }
}

void Economy::computeTrade() {
    trade_links_.clear();
    
    // Reset trade balances
    for (auto& region : regions_) {
        for (int g = 0; g < kGoodTypes; ++g) {
            region.trade_balance[g] = 0.0;
        }
    }
    
    if (!trade_network_) return;
    
    // Build production and demand vectors
    std::vector<std::array<double, kGoodTypes>> production(regions_.size());
    std::vector<std::array<double, kGoodTypes>> demand(regions_.size());
    std::vector<std::uint32_t> population(regions_.size());
    
    for (std::size_t i = 0; i < regions_.size(); ++i) {
        production[i] = regions_[i].production;
        population[i] = regions_[i].population;
        
        // EMERGENT DEMAND: Compute regional needs based on geography and development
        if (population[i] > 0) {
            double pop_density = population[i] / 500.0;
            RegionalNeeds needs = computeRegionalNeeds(regions_[i].x, regions_[i].y, 
                                                        regions_[i].development, pop_density);
            demand[i][FOOD] = population[i] * (needs.food + regions_[i].welfare * 0.2);
            demand[i][ENERGY] = population[i] * (needs.energy + regions_[i].welfare * 0.3);
            demand[i][TOOLS] = population[i] * (needs.tools + regions_[i].welfare * 0.2);
            demand[i][LUXURY] = population[i] * (needs.luxury + regions_[i].welfare * 0.5);
            demand[i][SERVICES] = population[i] * (needs.services + regions_[i].welfare * 0.4);
        } else {
            demand[i].fill(0.0);
        }
    }
    
    // Compute flows via matrix diffusion (single operation replaces all loops)
    auto trade_balances = trade_network_->computeFlows(production, demand, population, 0.15);
    
    // Apply trade balances to regions
    for (std::size_t i = 0; i < regions_.size(); ++i) {
        regions_[i].trade_balance = trade_balances[i];
    }
}

void Economy::computeConsumption() {
    for (auto& region : regions_) {
        for (int g = 0; g < kGoodTypes; ++g) {
            // Consumption = local production + net imports
            region.consumption[g] = region.production[g] + region.trade_balance[g];
            region.consumption[g] = std::max(0.0, region.consumption[g]);
        }
    }
}

void Economy::updatePrices() {
    // Adjust prices based on supply/demand balance
    for (auto& region : regions_) {
        if (region.population == 0) continue;
        
        // Get regional needs for price calculation
        double pop_density = region.population / 500.0;
        RegionalNeeds needs = computeRegionalNeeds(region.x, region.y, region.development, pop_density);
        
        for (int g = 0; g < kGoodTypes; ++g) {
            double supply = region.production[g];
            double subsistence = (g == FOOD) ? needs.food : 
                               (g == ENERGY) ? needs.energy :
                               (g == TOOLS) ? needs.tools :
                               (g == SERVICES) ? needs.services : needs.luxury;
            double demand = region.population * (subsistence + region.welfare * 0.5);  // demand increases with welfare
            
            double supply_demand_ratio = (demand > 0) ? (supply / demand) : 1.0;
            
            // Price adjusts: high demand → higher price, high supply → lower price
            if (supply_demand_ratio < 0.8) {
                // Shortage → price increases
                region.prices[g] *= (1.0 + PRICE_ADJUSTMENT_RATE);
            } else if (supply_demand_ratio > 1.2) {
                // Surplus → price decreases
                region.prices[g] *= (1.0 - PRICE_ADJUSTMENT_RATE * 0.5);
            }
            
            // EMERGENT PRICE BOUNDS: Extreme prices naturally correct through market forces
            // Very low prices attract buyers (demand spike), very high prices attract sellers (supply spike)
            // Only prevent numerical instability (not economic "reasonableness")
            double price = region.prices[g];
            if (price < 0.01) {
                // Price floor only for numerical stability - near-free goods get hoarded
                region.prices[g] = 0.01 + supply_demand_ratio * 0.05;
            } else if (price > 100.0) {
                // Ceiling only for numerical stability - hyperinflation triggers barter/alternatives
                region.prices[g] = 100.0 * (1.0 - (price - 100.0) / price * 0.1);
            }
        }
    }
}

void Economy::distributeIncome(const std::vector<Agent>& agents,
                               const std::vector<std::vector<std::uint32_t>>* region_index) {
    // Distribute income to agents based on productivity and regional economy
    // This creates wealth inequality over time
    
    if (region_index != nullptr) {
        // OPTIMIZED PATH: Use region_index for O(N) with locality
        for (std::size_t i = 0; i < regions_.size() && i < region_index->size(); ++i) {
            auto& region = regions_[i];
            const auto& agent_ids = (*region_index)[i];
            if (agent_ids.empty()) continue;
            
            // Compute total productivity for this region (one pass over region's agents)
            double region_total_productivity = 0.0;
            for (auto agent_id : agent_ids) {
                if (agent_id < agents_.size()) {
                    region_total_productivity += agents_[agent_id].productivity;
                }
            }
            
            if (region_total_productivity == 0.0) {
                // All agents in region have zero productivity
                for (auto agent_id : agent_ids) {
                    if (agent_id < agents_.size()) {
                        agents_[agent_id].income = 0.0;
                        agents_[agent_id].hardship = 1.0;
                    }
                }
                continue;
            }
            
            // Distribute income to agents in this region (one pass)
            double regional_avg_wealth = 0.0;
            if (region.population > 0) {
                double total_sector_production = 0.0;
                for (int g = 0; g < kGoodTypes; ++g) {
                    total_sector_production += region.production[g];
                }
                regional_avg_wealth = total_sector_production / region.population;
            } else {
                regional_avg_wealth = 1.0;
            }
            
            for (auto agent_id : agent_ids) {
                if (agent_id >= agents_.size()) continue;
                auto& agent = agents_[agent_id];
                
                // Agent income = (their productivity / total productivity) × regional production in their sector
                double sector_production = region.production[agent.sector];
                double income_share = agent.productivity / region_total_productivity;
                double base_income = sector_production * income_share * region.prices[agent.sector];
                
                // Regional efficiency effect (apply BEFORE consumption decisions)
                double regional_multiplier = 0.8 + region.efficiency * 0.4;
                base_income *= regional_multiplier;
                
                // Wealth begets wealth (capital returns on existing wealth)
                double wealth_return = std::log1p(agent.wealth) * 0.01;
                base_income += wealth_return;
                
                // Competition/position effect
                double relative_position = agent.wealth / std::max(0.1, regional_avg_wealth);
                if (relative_position > 2.0) {
                    base_income *= 1.0 + 0.1 * std::min(1.0, (relative_position - 2.0));
                } else if (relative_position < 0.5) {
                    base_income *= 0.9 + 0.2 * relative_position;
                }
                
                agent.income = base_income;
                
                // REALISTIC CONSUMPTION MODEL:
                // Budget constraint: consume from income, optionally dip into savings
                double subsistence_cost = region.prices[FOOD] * 0.7 + 
                                         region.prices[ENERGY] * 0.35 + 
                                         region.prices[SERVICES] * 0.15;
                
                double consumption, savings;
                if (base_income >= subsistence_cost * 1.2) {
                    // Comfortable: save 20% of income
                    consumption = base_income * 0.8;
                    savings = base_income * 0.2;
                } else if (base_income >= subsistence_cost) {
                    // Paycheck-to-paycheck: small savings
                    double surplus = base_income - subsistence_cost;
                    savings = surplus * 0.5;  // Save half of surplus
                    consumption = base_income - savings;
                } else {
                    // Poverty: consume all income, may dip into wealth for basics
                    consumption = base_income;
                    savings = 0.0;
                    // Can draw from wealth for necessities (up to 5% per tick)
                    double deficit = (subsistence_cost - base_income) * 0.5;  // Only cover half the gap
                    double wealth_draw = std::min(deficit, agent.wealth * 0.05);
                    agent.wealth -= wealth_draw;
                    consumption += wealth_draw;  // This spending helps reduce hardship
                }
                agent.wealth = std::max(0.01, agent.wealth + savings);  // Small floor prevents 0
                
                // Productivity evolution with realistic dynamics
                // Growth when young/learning, decay when skills become obsolete
                if (agent.productivity < 3.0) {
                    // Slow growth (learning by doing)
                    double growth_rate = 0.0003 * (1.0 + regional_avg_wealth * 0.1);  // Better regions = faster learning
                    agent.productivity *= (1.0 + growth_rate);
                }
                // Skill depreciation (technology changes, aging)
                agent.productivity *= 0.9999;  // ~1% decay per 100 ticks
                agent.productivity = std::max(0.2, agent.productivity);  // Floor
                
                // Compute agent hardship based on essential goods affordability
                double essential_cost = region.prices[FOOD] * 0.7 + 
                                       region.prices[ENERGY] * 0.35;
                double consumption_capacity = agent.income / essential_cost;
                agent.hardship = (consumption_capacity < 1.0) ? (1.0 - consumption_capacity) : 0.0;
                agent.hardship = std::clamp(agent.hardship, 0.0, 1.0);
            }
            
            // Update regional wealth distribution metrics (one pass over region)
            std::vector<double> wealths;
            wealths.reserve(agent_ids.size());
            for (auto agent_id : agent_ids) {
                if (agent_id < agents_.size()) {
                    wealths.push_back(agents_[agent_id].wealth);
                }
            }
            
            if (!wealths.empty()) {
                std::sort(wealths.begin(), wealths.end());
                
                std::size_t top_10_start = wealths.size() * 9 / 10;
                double top_10_wealth = std::accumulate(wealths.begin() + top_10_start, wealths.end(), 0.0);
                double total_wealth = std::accumulate(wealths.begin(), wealths.end(), 0.0);
                region.wealth_top_10 = (total_wealth > 0) ? (top_10_wealth / total_wealth) : 0.0;
                
                std::size_t bottom_50_end = wealths.size() / 2;
                double bottom_50_wealth = std::accumulate(wealths.begin(), wealths.begin() + bottom_50_end, 0.0);
                region.wealth_bottom_50 = (total_wealth > 0) ? (bottom_50_wealth / total_wealth) : 0.0;
            }
        }
    } else {
        // FALLBACK PATH: Original O(N) implementation
        // First pass: compute total productivity per region
        std::vector<double> region_total_productivity(regions_.size(), 0.0);
        
        for (std::size_t i = 0; i < agents_.size(); ++i) {
            std::uint32_t region_id = agents[i].region;
            
            if (region_id >= regions_.size()) {
                throw std::runtime_error("Invalid agent.region in distributeIncome: " + 
                                       std::to_string(region_id) +
                                       " (must be < " + std::to_string(regions_.size()) + ")");
            }
            
            region_total_productivity[region_id] += agents_[i].productivity;
        }
        
        // Second pass: distribute regional income to agents
        for (std::size_t i = 0; i < agents_.size(); ++i) {
            std::uint32_t region_id = agents[i].region;
            
            if (region_id >= regions_.size()) {
                throw std::runtime_error("Invalid agent.region in distributeIncome (second pass): " + 
                                       std::to_string(region_id) + 
                                       " (must be < " + std::to_string(regions_.size()) + ")");
            }
            
            auto& region = regions_[region_id];
            auto& agent = agents_[i];
            
            if (region_total_productivity[region_id] == 0.0) {
                agent.income = 0.0;
                agent.hardship = 1.0;
                continue;
            }
            
            double sector_production = region.production[agent.sector];
            double income_share = agent.productivity / region_total_productivity[region_id];
            double base_income = sector_production * income_share * region.prices[agent.sector];
            
            // Regional efficiency (apply before consumption)
            double regional_multiplier = 0.8 + region.efficiency * 0.4;
            base_income *= regional_multiplier;
            
            // Wealth returns
            double wealth_return = std::log1p(agent.wealth) * 0.01;
            base_income += wealth_return;
            
            // Regional avg wealth (use total production for consistency with optimized path)
            double total_sector_production = 0.0;
            for (int g = 0; g < kGoodTypes; ++g) {
                total_sector_production += region.production[g];
            }
            double regional_avg_wealth = (region.population > 0) 
                ? (total_sector_production / region.population) : 1.0;
            double relative_position = agent.wealth / std::max(0.1, regional_avg_wealth);
            
            if (relative_position > 2.0) {
                base_income *= 1.0 + 0.1 * std::min(1.0, (relative_position - 2.0));
            } else if (relative_position < 0.5) {
                base_income *= 0.9 + 0.2 * relative_position;
            }
            
            agent.income = base_income;
            
            // Realistic consumption model (same as optimized path)
            double subsistence_cost = region.prices[FOOD] * 0.7 + 
                                     region.prices[ENERGY] * 0.35 + 
                                     region.prices[SERVICES] * 0.15;
            
            double consumption, savings;
            if (base_income >= subsistence_cost * 1.2) {
                consumption = base_income * 0.8;
                savings = base_income * 0.2;
            } else if (base_income >= subsistence_cost) {
                double surplus = base_income - subsistence_cost;
                savings = surplus * 0.5;
                consumption = base_income - savings;
            } else {
                consumption = base_income;
                savings = 0.0;
                double deficit = (subsistence_cost - base_income) * 0.5;
                double wealth_draw = std::min(deficit, agent.wealth * 0.05);
                agent.wealth -= wealth_draw;
                consumption += wealth_draw;
            }
            agent.wealth = std::max(0.01, agent.wealth + savings);
            
            // Productivity with growth and decay
            if (agent.productivity < 3.0) {
                double growth_rate = 0.0003 * (1.0 + regional_avg_wealth * 0.1);
                agent.productivity *= (1.0 + growth_rate);
            }
            agent.productivity *= 0.9999;
            agent.productivity = std::max(0.2, agent.productivity);
            
            // Hardship based on essentials
            double essential_cost = region.prices[FOOD] * 0.7 + region.prices[ENERGY] * 0.35;
            double consumption_capacity = agent.income / essential_cost;
            agent.hardship = (consumption_capacity < 1.0) ? (1.0 - consumption_capacity) : 0.0;
            agent.hardship = std::clamp(agent.hardship, 0.0, 1.0);
        }
        
        // Third pass: Update regional wealth distribution metrics
        for (std::size_t r = 0; r < regions_.size(); ++r) {
            if (regions_[r].population == 0) continue;
            
            std::vector<double> wealths;
            for (std::size_t i = 0; i < agents_.size(); ++i) {
                if (agents[i].region == r) {
                    wealths.push_back(agents_[i].wealth);
                }
            }
            
            if (wealths.empty()) continue;
            
            std::sort(wealths.begin(), wealths.end());
            
            std::size_t top_10_start = wealths.size() * 9 / 10;
            double top_10_wealth = std::accumulate(wealths.begin() + top_10_start, wealths.end(), 0.0);
            double total_wealth = std::accumulate(wealths.begin(), wealths.end(), 0.0);
            regions_[r].wealth_top_10 = (total_wealth > 0) ? (top_10_wealth / total_wealth) : 0.0;
            
            std::size_t bottom_50_end = wealths.size() / 2;
            double bottom_50_wealth = std::accumulate(wealths.begin(), wealths.begin() + bottom_50_end, 0.0);
            regions_[r].wealth_bottom_50 = (total_wealth > 0) ? (bottom_50_wealth / total_wealth) : 0.0;
        }
    }
}

void Economy::evolveDevelopment() {
    for (auto& region : regions_) {
        if (region.population == 0) continue;
        
        // Development grows with welfare surplus, decays with hardship
        if (region.hardship < 0.3 && region.welfare > 1.2) {
            // Prosperous region - accumulate capital
            region.development += DEVELOPMENT_GROWTH_RATE * (region.welfare - 1.0);
        } else if (region.hardship > 0.5) {
            // Hardship erodes development
            region.development -= DEVELOPMENT_DECAY_RATE * region.hardship;
        }
        
        region.development = std::max(0.0, std::min(10.0, region.development));
    }
}

RegionalBeliefProfile Economy::analyzeRegionalBeliefs(
    uint32_t region_id,
    const std::vector<Agent>& agents,
    const std::vector<std::vector<std::uint32_t>>& region_index) const {
    
    RegionalBeliefProfile profile;
    
    const auto& agent_indices = region_index[region_id];
    if (agent_indices.empty()) {
        return profile;  // Return zero-initialized profile
    }
    
    // Pass 1: Compute means
    for (std::uint32_t idx : agent_indices) {
        const auto& agent = agents[idx];
        for (int d = 0; d < 4; ++d) {
            profile.mean[d] += agent.B[d];
        }
    }
    double n = static_cast<double>(agent_indices.size());
    for (int d = 0; d < 4; ++d) {
        profile.mean[d] /= n;
    }
    
    // Pass 2: Compute variance and track positive/negative faction sizes
    std::array<double, 4> sum_sq{0,0,0,0};
    std::array<int, 4> pos_count{0,0,0,0};
    std::array<int, 4> neg_count{0,0,0,0};
    std::array<double, 4> pos_sum{0,0,0,0};
    std::array<double, 4> neg_sum{0,0,0,0};
    
    for (std::uint32_t idx : agent_indices) {
        const auto& agent = agents[idx];
        for (int d = 0; d < 4; ++d) {
            double diff = agent.B[d] - profile.mean[d];
            sum_sq[d] += diff * diff;
            
            // Track positive and negative believers separately
            if (agent.B[d] > 0.1) {
                pos_count[d]++;
                pos_sum[d] += agent.B[d];
            } else if (agent.B[d] < -0.1) {
                neg_count[d]++;
                neg_sum[d] += agent.B[d];
            }
        }
    }
    
    // Compute variance and dominant pole for each dimension
    for (int d = 0; d < 4; ++d) {
        profile.variance[d] = sum_sq[d] / n;
        
        // Determine dominant pole: which faction is larger and more intense?
        // Use faction size weighted by average intensity
        double pos_weight = (pos_count[d] > 0) 
            ? pos_count[d] * (pos_sum[d] / pos_count[d])  // count * avg intensity
            : 0.0;
        double neg_weight = (neg_count[d] > 0)
            ? neg_count[d] * std::abs(neg_sum[d] / neg_count[d])  // count * avg |intensity|
            : 0.0;
        
        // Dominant pole is the direction of the stronger faction
        if (pos_weight > neg_weight * 1.2) {
            // Positive faction dominates - use positive average
            profile.dominant_pole[d] = (pos_count[d] > 0) ? pos_sum[d] / pos_count[d] : 0.0;
        } else if (neg_weight > pos_weight * 1.2) {
            // Negative faction dominates - use negative average
            profile.dominant_pole[d] = (neg_count[d] > 0) ? neg_sum[d] / neg_count[d] : 0.0;
        } else {
            // Neither dominates (within 20%) - truly contested, use 0
            profile.dominant_pole[d] = 0.0;
        }
    }
    
    // Overall polarization is average variance across dimensions
    profile.polarization = (profile.variance[0] + profile.variance[1] + 
                           profile.variance[2] + profile.variance[3]) / 4.0;
    
    return profile;
}

void Economy::evolveEconomicSystems(const std::vector<std::array<double, 4>>& region_belief_centroids) {
    if (forced_model_ != "") {
        // Global policy override
        for (auto& region : regions_) {
            region.economic_system = forced_model_;
            region.system_stability = 0.5;  // forced systems less stable
        }
        return;
    }
    
    // Economic systems emerge from beliefs + material conditions
    for (std::size_t i = 0; i < regions_.size(); ++i) {
        auto& region = regions_[i];
        const auto& beliefs = region_belief_centroids[i];
        
        // INCREMENT YEARS IN CURRENT SYSTEM (path dependence tracking)
        region.years_in_current_system++;
        
        // INSTITUTIONAL INERTIA: Increases over time (path dependence)
        // Long-established systems become harder to change
        double time_lock = std::min(0.3, region.years_in_current_system * 0.005);  // Cap at 30% from time
        region.institutional_inertia = std::min(0.9, 
            region.institutional_inertia * 0.99 + time_lock);
        
        std::string ideal_system = determineEconomicSystem(beliefs, region.development, 
                                                           region.hardship, region.inequality);
        
        // EMERGENT SYSTEM TRANSITION WITH HYSTERESIS AND PATH DEPENDENCE
        // Systems require sustained pressure over multiple ticks to change
        // This prevents thrashing between systems in crisis regions
        if (region.economic_system != ideal_system) {
            // Check if we're continuing pressure toward the same target
            if (region.pending_system == ideal_system) {
                // Same pressure direction - accumulate
                double hardship_pressure = std::max(0.0, (region.hardship - 0.3) * 0.5);
                double prosperity_pressure = std::max(0.0, (region.welfare - 0.8) * 0.3);
                double instability_pressure = (1.0 - region.system_stability) * 0.2;
                double inequality_pressure = std::max(0.0, (region.inequality - 0.4) * 0.3);
                double total_pressure = hardship_pressure + prosperity_pressure + instability_pressure + inequality_pressure;
                
                // APPLY INSTITUTIONAL INERTIA: Established systems resist change
                double inertia_factor = 1.0 - region.institutional_inertia;
                double adjusted_pressure = total_pressure * inertia_factor;
                
                // Pressure threshold determines how fast we accumulate transition ticks
                int pressure_increment = (adjusted_pressure > 0.5) ? 2 : 
                                        (adjusted_pressure > 0.2) ? 1 : 0;
                region.transition_pressure_ticks += pressure_increment;
                
                // Stability degrades during transition pressure
                region.system_stability = std::max(0.2, region.system_stability - 0.01 * adjusted_pressure);
                
                // DYNAMIC TRANSITION THRESHOLD: Entrenched systems need more sustained pressure
                int required_ticks = static_cast<int>(
                    RegionalEconomy::TRANSITION_THRESHOLD + region.years_in_current_system * 0.5
                );
                required_ticks = std::min(required_ticks, 200);  // Cap at 200 ticks (~20 years)
                
                // Check if we've reached the threshold for transition
                if (region.transition_pressure_ticks >= required_ticks) {
                    // Transition happens! This is a major disruption
                    region.economic_system = ideal_system;
                    region.pending_system = "";
                    region.transition_pressure_ticks = 0;
                    region.years_in_current_system = 0;  // Reset: new system
                    region.institutional_inertia *= 0.5;  // Disruption reduces inertia
                    region.system_stability = 0.3;  // New systems start unstable
                    
                    // Log the system change (if event logging available)
                    // event_log_.logSystemChange(generation, i, old_system, ideal_system);
                }
            } else {
                // Different pressure direction - reset and start new accumulation
                // But pressure relief is slowed by inertia (system resists even oscillation)
                region.pending_system = ideal_system;
                region.transition_pressure_ticks = static_cast<int>(
                    region.transition_pressure_ticks * (0.9 + region.institutional_inertia * 0.08)
                );
                if (region.transition_pressure_ticks < 1) {
                    region.transition_pressure_ticks = 1;
                }
            }
        } else {
            // System matches ideal - no pressure, recover stability
            region.pending_system = "";
            // Pressure decay is also affected by inertia (stable systems shed pressure slowly)
            region.transition_pressure_ticks = static_cast<int>(
                region.transition_pressure_ticks * (0.8 + region.institutional_inertia * 0.15)
            );
            region.system_stability = std::min(1.0, region.system_stability + 0.02);
        }
        
        // EMERGENT EFFICIENCY: Based on actual production performance, not system labels
        // Efficiency emerges from: stability, development, and production/consumption ratio
        double production_total = 0.0;
        double consumption_total = 0.0;
        for (int g = 0; g < kGoodTypes; ++g) {
            production_total += region.production[g];
            consumption_total += region.consumption[g];
        }
        
        // Base efficiency from how well production meets consumption needs
        double production_efficiency = (consumption_total > 0) 
            ? std::min(1.0, production_total / (consumption_total + 1.0))
            : 0.5;
        
        // Stability and development contribute to efficiency
        double stability_bonus = region.system_stability * 0.2;
        double development_bonus = std::min(0.2, region.development * 0.04);
        
        // Emergent efficiency: base + stability + development
        region.efficiency = std::clamp(
            0.5 + production_efficiency * 0.3 + stability_bonus + development_bonus,
            0.3, 1.0
        );
        
        // NOTE: Inequality is computed separately in computeInequality() from actual agent wealth
        // No hardcoded inequality values here - it's fully emergent!
    }
}

void Economy::evolveEconomicSystems(
    const std::vector<Agent>& agents,
    const std::vector<std::vector<std::uint32_t>>& region_index) {
    
    if (forced_model_ != "") {
        // Global policy override
        for (auto& region : regions_) {
            region.economic_system = forced_model_;
            region.system_stability = 0.5;  // forced systems less stable
        }
        return;
    }
    
    // Economic systems emerge from beliefs + material conditions
    // Now using DOMINANT POLE analysis for differentiated outcomes
    for (std::size_t i = 0; i < regions_.size(); ++i) {
        auto& region = regions_[i];
        
        // Analyze regional beliefs with dominant pole detection
        RegionalBeliefProfile profile = analyzeRegionalBeliefs(
            static_cast<uint32_t>(i), agents, region_index);
        
        // INCREMENT YEARS IN CURRENT SYSTEM (path dependence tracking)
        region.years_in_current_system++;
        
        // INSTITUTIONAL INERTIA: Increases over time (path dependence)
        double time_lock = std::min(0.3, region.years_in_current_system * 0.005);
        region.institutional_inertia = std::min(0.9, 
            region.institutional_inertia * 0.99 + time_lock);
        
        // Use dominant pole for system determination - NOT mean!
        std::string ideal_system = determineEconomicSystem(profile, region.development, 
                                                           region.hardship, region.inequality);
        
        // EMERGENT SYSTEM TRANSITION WITH HYSTERESIS AND PATH DEPENDENCE
        if (region.economic_system != ideal_system) {
            if (region.pending_system == ideal_system) {
                double hardship_pressure = std::max(0.0, (region.hardship - 0.3) * 0.5);
                double prosperity_pressure = std::max(0.0, (region.welfare - 0.8) * 0.3);
                double instability_pressure = (1.0 - region.system_stability) * 0.2;
                double inequality_pressure = std::max(0.0, (region.inequality - 0.4) * 0.3);
                double total_pressure = hardship_pressure + prosperity_pressure + instability_pressure + inequality_pressure;
                
                double inertia_factor = 1.0 - region.institutional_inertia;
                double adjusted_pressure = total_pressure * inertia_factor;
                
                int pressure_increment = (adjusted_pressure > 0.5) ? 2 : 
                                        (adjusted_pressure > 0.2) ? 1 : 0;
                region.transition_pressure_ticks += pressure_increment;
                
                region.system_stability = std::max(0.2, region.system_stability - 0.01 * adjusted_pressure);
                
                int required_ticks = static_cast<int>(
                    RegionalEconomy::TRANSITION_THRESHOLD + region.years_in_current_system * 0.5
                );
                required_ticks = std::min(required_ticks, 200);
                
                if (region.transition_pressure_ticks >= required_ticks) {
                    region.economic_system = ideal_system;
                    region.pending_system = "";
                    region.transition_pressure_ticks = 0;
                    region.years_in_current_system = 0;
                    region.institutional_inertia *= 0.5;
                    region.system_stability = 0.3;
                }
            } else {
                region.pending_system = ideal_system;
                region.transition_pressure_ticks = static_cast<int>(
                    region.transition_pressure_ticks * (0.9 + region.institutional_inertia * 0.08)
                );
                if (region.transition_pressure_ticks < 1) {
                    region.transition_pressure_ticks = 1;
                }
            }
        } else {
            region.pending_system = "";
            region.transition_pressure_ticks = static_cast<int>(
                region.transition_pressure_ticks * (0.8 + region.institutional_inertia * 0.15)
            );
            region.system_stability = std::min(1.0, region.system_stability + 0.02);
        }
        
        // EMERGENT EFFICIENCY
        double production_total = 0.0;
        double consumption_total = 0.0;
        for (int g = 0; g < kGoodTypes; ++g) {
            production_total += region.production[g];
            consumption_total += region.consumption[g];
        }
        
        double production_efficiency = (consumption_total > 0) 
            ? std::min(1.0, production_total / (consumption_total + 1.0))
            : 0.5;
        
        double stability_bonus = region.system_stability * 0.2;
        double development_bonus = std::min(0.2, region.development * 0.04);
        
        region.efficiency = std::clamp(
            0.5 + production_efficiency * 0.3 + stability_bonus + development_bonus,
            0.3, 1.0
        );
    }
}

Economy::StartConditionProfile Economy::resolveStartCondition(const std::string& name) const {
    auto normalize = [](const std::string& raw) {
        std::string normalized;
        normalized.reserve(raw.size());
        for (unsigned char ch : raw) {
            if (std::isalnum(ch)) {
                normalized.push_back(static_cast<char>(std::tolower(ch)));
            }
        }
        if (normalized.empty()) {
            normalized = "baseline";
        }
        return normalized;
    };

    auto make_profile = [](const std::string& canonical,
                           const std::array<double, kGoodTypes>& multipliers,
                           double base_dev,
                           double jitter,
                           std::string default_system,
                           double wealth_mean,
                           double wealth_std,
                           double prod_mean,
                           double prod_std) {
        StartConditionProfile profile;
        profile.name = canonical;
        profile.endowmentMultipliers = multipliers;
        profile.baseDevelopment = base_dev;
        profile.developmentJitter = jitter;
        profile.defaultSystem = std::move(default_system);
        profile.wealthLogMean = wealth_mean;
        profile.wealthLogStd = wealth_std;
        profile.productivityMean = prod_mean;
        profile.productivityStd = prod_std;
        return profile;
    };

    const std::string normalized = normalize(name);

    if (normalized == "baseline") {
        return make_profile("baseline",
                            {1.0, 1.0, 1.0, 0.85, 0.95},
                            0.8,
                            0.25,
                            "mixed",
                            0.1,
                            0.65,
                            1.0,
                            0.25);
    }

    if (normalized == "postscarcity" || normalized == "abundance" || normalized == "utopia") {
        return make_profile("postscarcity",
                            {1.2, 1.1, 1.05, 1.35, 1.45},
                            2.4,
                            0.15,
                            "cooperative",
                            0.3,
                            0.35,
                            1.2,
                            0.2);
    }

    if (normalized == "feudal" || normalized == "agrarian" || normalized == "lowtech") {
        return make_profile("feudal",
                            {1.4, 0.6, 0.4, 0.2, 0.25},
                            0.35,
                            0.08,
                            "feudal",
                            -0.7,
                            1.05,
                            0.75,
                            0.35);
    }

    if (normalized == "industrial" || normalized == "industrializing" || normalized == "boom") {
        return make_profile("industrial",
                            {0.9, 1.25, 1.35, 0.9, 0.95},
                            1.4,
                            0.30,
                            "market",
                            0.15,
                            0.55,
                            1.1,
                            0.35);
    }

    if (normalized == "crisis" || normalized == "collapse" || normalized == "depression") {
        return make_profile("crisis",
                            {0.65, 0.7, 0.75, 0.55, 0.6},
                            0.6,
                            0.2,
                            "mixed",
                            -0.2,
                            0.9,
                            0.9,
                            0.4);
    }

    std::cerr << "[Economy] Unknown start condition '" << name
              << "'. Falling back to 'baseline'.\n";
    return make_profile("baseline",
                        {1.0, 1.0, 1.0, 0.85, 0.95},
                        0.8,
                        0.25,
                        "mixed",
                        0.1,
                        0.65,
                        1.0,
                        0.25);
}

std::string Economy::determineEconomicSystem(const std::array<double, 4>& beliefs,
                                             double development,
                                             double hardship,
                                             double inequality) const {
    // Belief axes: [0]=Authority, [1]=Tradition, [2]=Hierarchy, [3]=Faith
    // Negative = (Liberty, Progress, Equality, Rationalism)
    
    double authority = beliefs[0];
    double tradition = beliefs[1];
    double hierarchy = beliefs[2];
    
    // Low development → feudal or cooperative (subsistence economies)
    if (development < 0.6) {
        if (hierarchy > 0.15 && authority > 0.1) {
            return "feudal";  // traditional hierarchy
        } else if (hierarchy < -0.1) {
            return "cooperative";  // communal subsistence
        }
    }
    
    // Crisis conditions → revolutionary pressure
    if (hardship > 0.4 && inequality > 0.5) {
        if (hierarchy < -0.1) {  // egalitarian beliefs
            return "planned";  // equality-seeking planned economy
        } else if (authority > 0.1) {
            return "feudal";  // strongman restoration
        }
    }
    
    // Developed + liberty + equality → cooperative/social democracy
    if (development > 1.2 && authority < -0.15 && hierarchy < -0.15) {
        return "cooperative";
    }
    
    // Developed + liberty + accepts hierarchy → market capitalism
    if (development > 0.8 && authority < -0.1 && hierarchy > 0.05) {
        return "market";
    }
    
    // Developed + authority + equality-leaning → planned economy
    if (development > 0.8 && authority > 0.15 && hierarchy < 0.1) {
        return "planned";
    }
    
    // Traditional + hierarchical but developed → feudal remnants
    if (tradition > 0.2 && hierarchy > 0.2 && development < 1.0) {
        return "feudal";
    }
    
    // Default: mixed economy (most common in moderate conditions)
    return "mixed";
}

std::string Economy::determineEconomicSystem(const RegionalBeliefProfile& profile,
                                             double development,
                                             double hardship,
                                             double inequality) const {
    // NEW: Use DOMINANT POLE instead of mean for system determination
    // This prevents averaging cancellation where opposing factions neutralize each other
    // Instead, the dominant faction's beliefs drive system selection
    
    // Belief axes: [0]=Authority, [1]=Tradition, [2]=Hierarchy, [3]=Faith
    // Negative = (Liberty, Progress, Equality, Rationalism)
    
    double authority = profile.dominant_pole[0];
    double tradition = profile.dominant_pole[1];
    double hierarchy = profile.dominant_pole[2];
    
    // ADJUSTED THRESHOLDS: Match actual simulation development range (0.5-1.0)
    // Previously required development > 1.2 which was unrealistic
    
    // Low development → feudal or cooperative (subsistence economies)
    if (development < 0.4) {
        if (hierarchy > 0.1 && authority > 0.05) {
            return "feudal";  // traditional hierarchy
        } else if (hierarchy < -0.05) {
            return "cooperative";  // communal subsistence
        }
    }
    
    // Crisis conditions → revolutionary pressure
    if (hardship > 0.35 && inequality > 0.45) {
        if (hierarchy < -0.05) {  // egalitarian beliefs
            return "planned";  // equality-seeking planned economy
        } else if (authority > 0.05) {
            return "feudal";  // strongman restoration
        }
    }
    
    // Highly polarized regions → system determined by stronger faction
    // This is where dominant pole really shines - contested areas have clear winners
    if (profile.polarization > 0.05) {
        // Polarized: use the dominant pole directly with LOWER thresholds
        if (development > 0.8 && authority < -0.1 && hierarchy < -0.1) {
            return "cooperative";
        }
        if (development > 0.5 && authority < -0.05 && hierarchy > 0.02) {
            return "market";
        }
        if (development > 0.5 && authority > 0.1 && hierarchy < 0.05) {
            return "planned";
        }
        if (hierarchy > 0.12 && authority > 0.08) {
            return "feudal";
        }
    }
    
    // Developed + liberty + equality → cooperative/social democracy
    if (development > 0.8 && authority < -0.1 && hierarchy < -0.1) {
        return "cooperative";
    }
    
    // Developed + liberty + accepts hierarchy → market capitalism  
    if (development > 0.5 && authority < -0.05 && hierarchy > 0.02) {
        return "market";
    }
    
    // Developed + authority + equality-leaning → planned economy
    if (development > 0.5 && authority > 0.1 && hierarchy < 0.05) {
        return "planned";
    }
    
    // Traditional + hierarchical → feudal remnants
    if (tradition > 0.1 && hierarchy > 0.12 && development < 0.7) {
        return "feudal";
    }
    
    // Default: mixed economy (only for truly contested regions with no dominant pole)
    return "mixed";
}


double Economy::computeRegionGini(std::uint32_t region_id, const std::vector<Agent>& agents) const {
    // Compute Gini coefficient for wealth distribution in a region using O(N log N) algorithm
    // Gini = 0 (perfect equality) to 1 (total inequality)
    
    // Validate region_id
    if (region_id >= regions_.size()) {
        throw std::runtime_error("Invalid region_id in computeRegionGini: " + 
                               std::to_string(region_id) + 
                               " (must be < " + std::to_string(regions_.size()) + ")");
    }
    
    if (agents_.empty()) return 0.0;
    
    // Collect agent wealths in this region using actual agent.region values
    std::vector<double> wealths;
    for (std::size_t i = 0; i < agents.size(); ++i) {
        if (agents[i].region == region_id) {
            wealths.push_back(agents_[i].wealth);
        }
    }
    
    if (wealths.size() < 2) return 0.0;
    
    // Sort wealths O(N log N)
    std::sort(wealths.begin(), wealths.end());
    
    // Compute Gini using efficient formula: Gini = (2 * weighted_sum) / (n * total_sum) - (n+1)/n
    // where weighted_sum = sum of (i+1) * wealth[i]
    double weighted_sum = 0.0;
    double total_sum = 0.0;
    
    for (std::size_t i = 0; i < wealths.size(); ++i) {
        weighted_sum += (i + 1) * wealths[i];
        total_sum += wealths[i];
    }
    
    if (total_sum == 0.0) return 0.0;
    
    std::size_t n = wealths.size();
    double gini = (2.0 * weighted_sum) / (n * total_sum) - (n + 1.0) / n;
    return gini;
}

const RegionalEconomy& Economy::getRegion(std::uint32_t region_id) const {
    return regions_[region_id];
}

RegionalEconomy& Economy::getRegionMut(std::uint32_t region_id) {
    return regions_[region_id];
}

AgentEconomy& Economy::getAgentEconomy(std::uint32_t agent_id) {
    return agents_[agent_id];
}

const AgentEconomy& Economy::getAgentEconomy(std::uint32_t agent_id) const {
    return agents_[agent_id];
}

void Economy::addAgent(std::uint32_t agent_id, std::uint32_t region_id, std::mt19937_64& rng) {
    // Ensure the agents_ vector is large enough
    if (agent_id >= agents_.size()) {
        agents_.resize(agent_id + 1);
    }
    
    // Initialize new agent's economy
    std::uniform_real_distribution<double> wealth_dist(0.5, 1.5);
    std::uniform_int_distribution<int> sector_dist(0, kGoodTypes - 1);
    
    AgentEconomy& agent = agents_[agent_id];
    agent.wealth = wealth_dist(rng);
    agent.income = 1.0;
    agent.productivity = 1.0;
    agent.sector = sector_dist(rng);
    agent.hardship = 0.0;
}

double Economy::globalWelfare() const {
    if (regions_.empty()) return 1.0;
    
    double total_welfare = 0.0;
    std::uint64_t total_population = 0;
    
    for (const auto& region : regions_) {
        total_welfare += region.welfare * region.population;
        total_population += region.population;
    }
    
    return (total_population > 0) ? (total_welfare / total_population) : 1.0;
}

double Economy::globalInequality() const {
    if (regions_.empty()) return 0.0;
    
    // Compute global Gini as population-weighted average of regional Ginis
    double total_inequality = 0.0;
    std::uint64_t total_population = 0;
    
    for (const auto& region : regions_) {
        total_inequality += region.inequality * region.population;
        total_population += region.population;
    }
    
    return (total_population > 0) ? (total_inequality / total_population) : 0.0;
}

double Economy::globalHardship() const {
    if (regions_.empty()) return 0.0;
    
    // Compute global hardship as population-weighted average
    double total_hardship = 0.0;
    std::uint64_t total_population = 0;
    
    for (const auto& region : regions_) {
        total_hardship += region.hardship * region.population;
        total_population += region.population;
    }
    
    return (total_population > 0) ? (total_hardship / total_population) : 0.0;
}

double Economy::globalDevelopment() const {
    if (regions_.empty()) return 0.0;
    
    double total_development = 0.0;
    std::uint64_t total_population = 0;
    
    for (const auto& region : regions_) {
        total_development += region.development * region.population;
        total_population += region.population;
    }
    
    return (total_population > 0) ? (total_development / total_population) : 0.0;
}

double Economy::getTotalTrade() const {
    // Sum absolute trade balance flows across all regions and goods
    // (each unit traded appears once as export, once as import, so divide by 2)
    double total = 0.0;
    for (const auto& region : regions_) {
        for (int g = 0; g < kGoodTypes; ++g) {
            total += std::abs(region.trade_balance[g]);
        }
    }
    return total / 2.0;  // Avoid double-counting
}

void Economy::setEconomicModel(const std::string& model) {
    if (model == "market" || model == "planned" || model == "mixed" || 
        model == "feudal" || model == "cooperative" || model == "") {
        forced_model_ = model;
    }
}

void Economy::reallocateToWar(double fraction) {
    war_allocation_ = std::max(0.0, std::min(1.0, fraction));
}
