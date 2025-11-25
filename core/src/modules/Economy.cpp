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

// Thread-local RNG for parallel operations (prevents race conditions)
namespace {
    thread_local std::mt19937_64 tl_rng{std::random_device{}()};
    
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
                    std::uint64_t generation) {
    // Update population counts
    for (std::size_t i = 0; i < regions_.size(); ++i) {
        regions_[i].population = region_populations[i];
    }
    
    // Economic evolution happens gradually
    if (generation % 10 == 0) {
        evolveSpecialization();
        evolveDevelopment();
        evolveEconomicSystems(region_belief_centroids);
    }
    
    computeProduction();
    computeTrade();
    computeConsumption();
    updatePrices();
    distributeIncome(agents);
    computeWelfare();
    computeInequality(agents);
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

void Economy::computeInequality(const std::vector<Agent>& agents) {
    // Compute Gini coefficient for each region based on agent wealth
    // This is now FULLY EMERGENT - no overrides based on economic system labels
    for (std::size_t i = 0; i < regions_.size(); ++i) {
        if (regions_[i].population == 0) {
            regions_[i].inequality = 0.0;
            continue;
        }
        
        // Compute Gini from agent wealth distribution - this is the TRUE inequality
        double gini = computeRegionGini(static_cast<std::uint32_t>(i), agents);
        
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

void Economy::distributeIncome(const std::vector<Agent>& agents) {
    // Distribute income to agents based on productivity and regional economy
    // This creates wealth inequality over time
    
    if (agents_.empty()) return;
    
    // First pass: compute total productivity per region
    std::vector<double> region_total_productivity(regions_.size(), 0.0);
    
    // Use actual agent.region values instead of sequential assignment
    for (std::size_t i = 0; i < agents_.size(); ++i) {
        std::uint32_t region_id = agents[i].region;
        
        // Validate agent-region mapping
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
        
        // Validate agent-region mapping
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
        
        // Agent income = (their productivity / total productivity) × regional production in their sector
        double sector_production = region.production[agent.sector];
        double income_share = agent.productivity / region_total_productivity[region_id];
        agent.income = sector_production * income_share * region.prices[agent.sector];
        
        // Consumption (agents must spend to maintain wealth)
        double consumption = std::min(agent.wealth, agent.income * 0.8);  // consume ~80% of income or available wealth
        agent.wealth -= consumption;
        
        // Wealth accumulation from savings
        agent.wealth += agent.income * 0.2;  // save 20% of income
        
        // Wealth can't go negative
        agent.wealth = std::max(0.0, agent.wealth);
        
        // EMERGENT INCOME DYNAMICS: Based on wealth, productivity, and regional conditions
        // No hardcoded system-based multipliers!
        
        // Wealth begets wealth (capital returns) - this is emergent from having capital
        double wealth_return = std::log1p(agent.wealth) * 0.01;  // diminishing returns
        agent.income += wealth_return;
        
        // Productivity compounds slowly based on experience (proxied by wealth accumulation)
        if (agent.productivity < 3.0) {
            agent.productivity *= (1.0 + 0.0005 * agent.productivity);  // slower compound growth
        }
        
        // Regional economic conditions affect all incomes
        double regional_multiplier = 0.8 + region.efficiency * 0.4;  // 0.8-1.2 based on efficiency
        agent.income *= regional_multiplier;
        
        // Competition effect: in regions with high inequality, median incomes compress
        // while top incomes grow (emergent Matthew effect)
        double regional_avg_wealth = (region.population > 0) 
            ? (sector_production / region.population) : 1.0;
        double relative_position = agent.wealth / std::max(0.1, regional_avg_wealth);
        
        if (relative_position > 2.0) {
            // Above-average wealth → slight income boost from network effects
            agent.income *= 1.0 + 0.1 * std::min(1.0, (relative_position - 2.0));
        } else if (relative_position < 0.5) {
            // Below-average wealth → slight income penalty from lack of capital
            agent.income *= 0.9 + 0.2 * relative_position;
        }
        
        // Compute agent hardship
        double consumption_capacity = agent.income / region.prices[FOOD];  // simplified
        agent.hardship = (consumption_capacity < 1.0) ? (1.0 - consumption_capacity) : 0.0;
    }
    
    // Update regional wealth distribution metrics
    for (std::size_t r = 0; r < regions_.size(); ++r) {
        if (regions_[r].population == 0) continue;
        
        // Collect agent wealths in this region using actual agent.region values
        std::vector<double> wealths;
        for (std::size_t i = 0; i < agents_.size(); ++i) {
            if (agents[i].region == r) {
                wealths.push_back(agents_[i].wealth);
            }
        }
        
        if (wealths.empty()) continue;
        
        std::sort(wealths.begin(), wealths.end());
        
        // Top 10% wealth share
        std::size_t top_10_start = wealths.size() * 9 / 10;
        double top_10_wealth = std::accumulate(wealths.begin() + top_10_start, wealths.end(), 0.0);
        double total_wealth = std::accumulate(wealths.begin(), wealths.end(), 0.0);
        regions_[r].wealth_top_10 = (total_wealth > 0) ? (top_10_wealth / total_wealth) : 0.0;
        
        // Bottom 50% wealth share
        std::size_t bottom_50_end = wealths.size() / 2;
        double bottom_50_wealth = std::accumulate(wealths.begin(), wealths.begin() + bottom_50_end, 0.0);
        regions_[r].wealth_bottom_50 = (total_wealth > 0) ? (bottom_50_wealth / total_wealth) : 0.0;
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
        
        std::string ideal_system = determineEconomicSystem(beliefs, region.development, 
                                                           region.hardship, region.inequality);
        
        // EMERGENT SYSTEM TRANSITION: Gradual probability-based change
        // Systems don't flip at magic thresholds - they shift under sustained pressure
        if (region.economic_system != ideal_system) {
            // Transition pressure builds from multiple sources
            double hardship_pressure = std::max(0.0, (region.hardship - 0.3) * 0.5); // starts building at 0.3
            double prosperity_pressure = std::max(0.0, (region.welfare - 0.8) * 0.3); // success enables experimentation
            double instability_pressure = (1.0 - region.system_stability) * 0.2; // unstable systems more likely to change
            double inequality_pressure = std::max(0.0, (region.inequality - 0.4) * 0.3); // high inequality → unrest
            
            double total_pressure = hardship_pressure + prosperity_pressure + instability_pressure + inequality_pressure;
            
            // Probabilistic transition: higher pressure = higher chance
            // Even low pressure has tiny chance; high pressure makes change likely but not guaranteed
            double transition_prob = std::min(0.5, total_pressure * 0.1); // max 50% per tick
            std::uniform_real_distribution<double> prob_dist(0.0, 1.0);
            static std::mt19937 sys_rng(42); // local RNG for system transitions
            
            if (prob_dist(sys_rng) < transition_prob) {
                region.economic_system = ideal_system;
                region.system_stability = 0.2 + 0.2 * (1.0 - total_pressure); // harder transitions = less stable result
            }
        } else {
            // Stable system
            region.system_stability = std::min(1.0, region.system_stability + 0.01);
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
    
    // Low development → feudal or cooperative
    if (development < 0.5) {
        if (hierarchy > 0.3 && authority > 0.2) {
            return "feudal";  // traditional hierarchy
        } else {
            return "cooperative";  // communal subsistence
        }
    }
    
    // High hardship + inequality → revolution toward planned
    if (hardship > 0.5 && inequality > 0.4) {
        if (hierarchy < -0.2) {  // egalitarian beliefs
            return "planned";  // equality-seeking planned economy
        }
    }
    
    // Developed + liberty + equality → cooperative
    if (development > 1.5 && authority < -0.3 && hierarchy < -0.3) {
        return "cooperative";  // democratic socialism
    }
    
    // Developed + liberty + hierarchy → market
    if (development > 1.0 && authority < -0.2 && hierarchy > 0.1) {
        return "market";  // liberal capitalism
    }
    
    // Developed + authority + equality → planned
    if (development > 1.0 && authority > 0.3 && hierarchy < 0.0) {
        return "planned";  // state socialism
    }
    
    // Default: mixed economy (most common)
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
