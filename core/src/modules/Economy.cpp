#include "modules/Economy.h"
#include <algorithm>
#include <numeric>
#include <cmath>
#include <random>

// Subsistence requirements per capita (basic needs) - reduced for early game bootstrap
constexpr double FOOD_SUBSISTENCE = 0.7;      // Was 1.0, reduced for bootstrap
constexpr double ENERGY_SUBSISTENCE = 0.35;   // Was 0.5, reduced for bootstrap  
constexpr double TOOLS_SUBSISTENCE = 0.2;     // Was 0.3, reduced for bootstrap
constexpr double LUXURY_SUBSISTENCE = 0.0;    // non-essential
constexpr double SERVICES_SUBSISTENCE = 0.15; // Was 0.2, reduced for bootstrap

// Development rates
constexpr double DEVELOPMENT_GROWTH_RATE = 0.01;  // per tick with surplus
constexpr double DEVELOPMENT_DECAY_RATE = 0.005;  // per tick with hardship

// Specialization evolution rate
constexpr double SPECIALIZATION_RATE = 0.001;  // per tick

// Price adjustment rate
constexpr double PRICE_ADJUSTMENT_RATE = 0.05;  // per tick based on supply/demand

// Transport cost (scales with distance)
constexpr double BASE_TRANSPORT_COST = 0.02;  // 2% per hop

void Economy::init(std::uint32_t num_regions, std::uint32_t num_agents, std::mt19937_64& rng) {
    regions_.clear();
    regions_.reserve(num_regions);
    trade_links_.clear();
    agents_.clear();
    
    for (std::uint32_t i = 0; i < num_regions; ++i) {
        RegionalEconomy region;
        region.region_id = i;
        region.development = 0.1;  // Start with minimal development
        region.economic_system = "mixed";  // Initial system
        region.system_stability = 1.0;
        
        regions_.push_back(region);
    }
    
    initializeEndowments(rng);
    initializeTradeNetwork();
    initializeAgents(num_agents, rng);
}

void Economy::update(const std::vector<std::uint32_t>& region_populations,
                    const std::vector<std::array<double, 4>>& region_belief_centroids,
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
    distributeIncome(region_populations);
    computeWelfare();
    computeInequality();
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

void Economy::computeInequality() {
    // Compute Gini coefficient for each region based on agent wealth
    for (std::size_t i = 0; i < regions_.size(); ++i) {
        if (regions_[i].population == 0) {
            regions_[i].inequality = 0.0;
            continue;
        }
        
        // Compute Gini from agent wealth distribution
        double gini = computeRegionGini(static_cast<std::uint32_t>(i), 
                                       std::vector<std::uint32_t>(regions_.size(), 0));
        
        // Also factor in economic system's inherent inequality
        if (regions_[i].economic_system == "market") {
            gini = std::max(gini, 0.35 + regions_[i].development * 0.05);
        } else if (regions_[i].economic_system == "planned") {
            gini = std::min(gini, 0.15 + regions_[i].development * 0.02);
        } else if (regions_[i].economic_system == "feudal") {
            gini = std::max(gini, 0.55 + regions_[i].development * 0.03);
        }
        
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
        
        double food_deficit = std::max(0.0, FOOD_SUBSISTENCE - food_per_capita) / FOOD_SUBSISTENCE;
        double energy_deficit = std::max(0.0, ENERGY_SUBSISTENCE - energy_per_capita) / ENERGY_SUBSISTENCE;
        double tools_deficit = std::max(0.0, TOOLS_SUBSISTENCE - tools_per_capita) / TOOLS_SUBSISTENCE;
        double services_deficit = std::max(0.0, SERVICES_SUBSISTENCE - services_per_capita) / SERVICES_SUBSISTENCE;
        
        // Weighted average (food and energy most critical)
        region.hardship = (food_deficit * 0.4 + energy_deficit * 0.3 + 
                          tools_deficit * 0.15 + services_deficit * 0.15);
        region.hardship = std::max(0.0, std::min(1.0, region.hardship));
    }
}

void Economy::initializeEndowments(std::mt19937_64& rng) {
    // Create geographic variation in resource endowments
    // High base production to allow for surpluses and trade
    // Each unit of endowment represents per-capita production capacity
    std::uniform_real_distribution<double> dist(0.8, 1.4);
    
    for (auto& region : regions_) {
        // Base endowments are per capita - will be multiplied by population in computeProduction
        region.endowments[FOOD] = dist(rng) * 2.0;       // ~1.6-2.8 per capita (boosted for bootstrap)
        region.endowments[ENERGY] = dist(rng) * 1.5;     // ~1.2-2.1 per capita (boosted for bootstrap)
        region.endowments[TOOLS] = dist(rng) * 1.2;      // ~1.0-1.7 per capita (boosted for bootstrap)
        region.endowments[LUXURY] = dist(rng) * 0.8;     // ~0.6-1.1 per capita (boosted for bootstrap)
        region.endowments[SERVICES] = dist(rng) * 1.0;   // ~0.8-1.4 per capita (boosted for bootstrap)
        
        // Initialize specialization at zero (will evolve)
        for (int g = 0; g < kGoodTypes; ++g) {
            region.specialization[g] = 0.0;
        }
    }
}

void Economy::initializeTradeNetwork() {
    // Simple trade network: each region trades with 5-10 neighbors
    // For now, use simple spatial proximity (adjacent region IDs)
    for (std::size_t i = 0; i < regions_.size(); ++i) {
        regions_[i].trade_partners.clear();
        
        // Trade with nearby regions (wrapping around)
        for (int offset = -5; offset <= 5; ++offset) {
            if (offset == 0) continue;
            int partner = (static_cast<int>(i) + offset + static_cast<int>(regions_.size())) 
                         % static_cast<int>(regions_.size());
            regions_[i].trade_partners.push_back(static_cast<std::uint32_t>(partner));
        }
    }
}

void Economy::initializeAgents(std::uint32_t num_agents, std::mt19937_64& rng) {
    agents_.clear();
    agents_.reserve(num_agents);
    
    std::uniform_real_distribution<double> wealth_dist(0.5, 1.5);
    std::uniform_int_distribution<int> sector_dist(0, kGoodTypes - 1);
    
    for (std::uint32_t i = 0; i < num_agents; ++i) {
        AgentEconomy agent;
        agent.wealth = wealth_dist(rng);
        agent.income = 1.0;
        agent.productivity = 1.0;
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
    
    // Compute trade flows based on surplus/deficit and prices
    for (auto& region : regions_) {
        if (region.population == 0) continue;
        
        for (int g = 0; g < kGoodTypes; ++g) {
            double per_capita = region.production[g] / region.population;
            double subsistence = (g == FOOD) ? FOOD_SUBSISTENCE : 
                               (g == ENERGY) ? ENERGY_SUBSISTENCE :
                               (g == TOOLS) ? TOOLS_SUBSISTENCE :
                               (g == SERVICES) ? SERVICES_SUBSISTENCE : LUXURY_SUBSISTENCE;
            
            double surplus = region.production[g] - (region.population * subsistence);
            
            if (surplus > 0.0) {
                // Region has surplus - export to deficient partners
                double available_export = surplus * 0.7;  // keep some reserve
                double export_per_partner = available_export / region.trade_partners.size();
                
                for (auto partner_id : region.trade_partners) {
                    auto& partner = regions_[partner_id];
                    double partner_deficit = (partner.population * subsistence) - partner.production[g];
                    
                    if (partner_deficit > 0.0) {
                        double trade_amount = std::min(export_per_partner, partner_deficit * 0.5);
                        
                        // Calculate transport cost based on "distance" (simplified)
                        int distance = std::abs(static_cast<int>(region.region_id) - static_cast<int>(partner_id));
                        distance = std::min(distance, static_cast<int>(regions_.size()) - distance);  // wrap around
                        double transport_cost = BASE_TRANSPORT_COST * distance;
                        
                        TradeLink link;
                        link.from_region = region.region_id;
                        link.to_region = partner_id;
                        link.good = static_cast<GoodType>(g);
                        link.volume = trade_amount;
                        link.transport_cost = transport_cost;
                        link.price = region.prices[g];
                        
                        trade_links_.push_back(link);
                        
                        region.trade_balance[g] -= trade_amount;  // export
                        partner.trade_balance[g] += trade_amount * (1.0 - transport_cost);  // import (minus transport loss)
                    }
                }
            }
        }
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
        
        for (int g = 0; g < kGoodTypes; ++g) {
            double supply = region.production[g];
            double subsistence = (g == FOOD) ? FOOD_SUBSISTENCE : 
                               (g == ENERGY) ? ENERGY_SUBSISTENCE :
                               (g == TOOLS) ? TOOLS_SUBSISTENCE :
                               (g == SERVICES) ? SERVICES_SUBSISTENCE : LUXURY_SUBSISTENCE;
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
            
            // Keep prices within reasonable bounds
            region.prices[g] = std::max(0.1, std::min(10.0, region.prices[g]));
        }
    }
}

void Economy::distributeIncome(const std::vector<std::uint32_t>& region_populations) {
    // Distribute income to agents based on productivity and regional economy
    // This creates wealth inequality over time
    
    if (agents_.empty()) return;
    
    // First pass: compute total productivity per region
    std::vector<double> region_total_productivity(regions_.size(), 0.0);
    std::vector<std::uint32_t> agent_region_map(agents_.size(), 0);
    
    // Map agents to regions (simplified: sequential distribution)
    std::uint32_t agent_idx = 0;
    for (std::size_t r = 0; r < regions_.size(); ++r) {
        std::uint32_t region_pop = region_populations[r];
        for (std::uint32_t i = 0; i < region_pop && agent_idx < agents_.size(); ++i) {
            agent_region_map[agent_idx] = static_cast<std::uint32_t>(r);
            region_total_productivity[r] += agents_[agent_idx].productivity;
            ++agent_idx;
        }
    }
    
    // Second pass: distribute regional income to agents
    for (std::size_t i = 0; i < agents_.size(); ++i) {
        std::uint32_t region_id = agent_region_map[i];
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
        
        // Wealth accumulation
        agent.wealth += agent.income * 0.1;  // save 10% of income
        
        // Economic system affects income distribution
        if (region.economic_system == "market") {
            // Market systems increase inequality (winner-take-all)
            agent.productivity *= 1.001;  // compound growth for productive agents
        } else if (region.economic_system == "planned") {
            // Planned systems compress income distribution
            agent.income = agent.income * 0.7 + (sector_production / region.population) * 0.3;
        } else if (region.economic_system == "feudal") {
            // Feudal systems concentrate wealth at top
            if (agent.wealth > 2.0) {
                agent.income *= 1.5;  // elites get rents
            } else {
                agent.income *= 0.7;  // peasants get less
            }
        }
        
        // Compute agent hardship
        double consumption_capacity = agent.income / region.prices[FOOD];  // simplified
        agent.hardship = (consumption_capacity < 1.0) ? (1.0 - consumption_capacity) : 0.0;
    }
    
    // Update regional wealth distribution metrics
    for (std::size_t r = 0; r < regions_.size(); ++r) {
        if (region_populations[r] == 0) continue;
        
        // Collect agent wealths in this region
        std::vector<double> wealths;
        for (std::size_t i = 0; i < agents_.size(); ++i) {
            if (agent_region_map[i] == r) {
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
        
        // Gradual transition toward ideal system
        if (region.economic_system != ideal_system) {
            // System change happens during crisis or prosperity
            if (region.hardship > 0.6 || region.welfare > 1.5) {
                region.economic_system = ideal_system;
                region.system_stability = 0.3;  // unstable during transition
            }
        } else {
            // Stable system
            region.system_stability = std::min(1.0, region.system_stability + 0.01);
        }
        
        // Set efficiency based on system and stability
        if (region.economic_system == "market") {
            region.efficiency = 0.9 + region.system_stability * 0.1;
            region.inequality = 0.35 + region.development * 0.05;
        } else if (region.economic_system == "planned") {
            region.efficiency = 0.65 + region.system_stability * 0.15;
            region.inequality = 0.15 + region.development * 0.02;
        } else if (region.economic_system == "cooperative") {
            region.efficiency = 0.75 + region.system_stability * 0.15;
            region.inequality = 0.20 + region.development * 0.03;
        } else if (region.economic_system == "feudal") {
            region.efficiency = 0.5 + region.system_stability * 0.1;
            region.inequality = 0.55 + region.development * 0.03;
        } else {  // mixed
            region.efficiency = 0.80 + region.system_stability * 0.1;
            region.inequality = 0.28 + region.development * 0.04;
        }
    }
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



double Economy::computeRegionGini(std::uint32_t region_id, const std::vector<std::uint32_t>& region_populations) const {
    // Compute Gini coefficient for wealth distribution in a region
    // Gini = 0 (perfect equality) to 1 (total inequality)
    
    if (agents_.empty()) return 0.0;
    
    // Collect agent wealths in this region
    std::vector<double> wealths;
    std::uint32_t agent_idx = 0;
    for (std::size_t r = 0; r <= region_id && r < region_populations.size(); ++r) {
        for (std::uint32_t i = 0; i < region_populations[r] && agent_idx < agents_.size(); ++i) {
            if (r == region_id) {
                wealths.push_back(agents_[agent_idx].wealth);
            }
            ++agent_idx;
        }
    }
    
    if (wealths.size() < 2) return 0.0;
    
    std::sort(wealths.begin(), wealths.end());
    
    // Compute Gini coefficient using standard formula
    double sum_abs_diff = 0.0;
    double sum_wealth = 0.0;
    
    for (std::size_t i = 0; i < wealths.size(); ++i) {
        for (std::size_t j = 0; j < wealths.size(); ++j) {
            sum_abs_diff += std::abs(wealths[i] - wealths[j]);
        }
        sum_wealth += wealths[i];
    }
    
    if (sum_wealth == 0.0) return 0.0;
    
    double gini = sum_abs_diff / (2.0 * wealths.size() * sum_wealth);
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
    double total = 0.0;
    for (const auto& link : trade_links_) {
        total += link.volume;
    }
    return total;
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
