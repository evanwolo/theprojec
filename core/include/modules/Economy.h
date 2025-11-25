#ifndef ECONOMY_H
#define ECONOMY_H

#include <array>
#include <memory>

#include "modules/EconomyTypes.h"
#include "modules/TradeNetwork.h"
#include <cstdint>
#include <string>
#include <random>

// Trade link between regions with transport costs
struct TradeLink {
    std::uint32_t from_region;
    std::uint32_t to_region;
    GoodType good;
    double volume;          // quantity traded
    double transport_cost;  // per unit (0.01 = 1% of value)
    double price;           // current market price
};

// Agent-level economic data
struct AgentEconomy {
    double wealth = 1.0;          // accumulated assets
    double income = 1.0;          // income per tick
    double productivity = 1.0;    // personal productivity multiplier
    int sector = 0;               // which good they produce (0-4)
    double hardship = 0.0;        // personal economic stress
};

// Production/consumption per region
struct RegionalEconomy {
    std::uint32_t region_id;
    
    // Geographic coordinates (0.0 to 1.0 normalized)
    double x = 0.0;  // west-east position
    double y = 0.0;  // south-north position
    
    // Endowments (base production capacity) - varies by geography
    std::array<double, kGoodTypes> endowments = {1.0, 1.0, 1.0, 0.5, 0.5};
    
    // Specialization level (0=diversified, 1=highly specialized)
    std::array<double, kGoodTypes> specialization = {0, 0, 0, 0, 0};
    
    // Current production (endowment × specialization × tech × efficiency)
    std::array<double, kGoodTypes> production = {0, 0, 0, 0, 0};
    
    // Consumption (local production + imports - exports)
    std::array<double, kGoodTypes> consumption = {0, 0, 0, 0, 0};
    
    // Prices (relative value, starts at 1.0)
    std::array<double, kGoodTypes> prices = {1.0, 1.0, 1.0, 1.0, 1.0};
    
    // Trade balance per good (exports - imports)
    std::array<double, kGoodTypes> trade_balance = {0, 0, 0, 0, 0};
    
    // Population in this region
    std::uint32_t population = 0;
    
    // Economic indicators
    double welfare = 1.0;           // consumption per capita
    double inequality = 0.0;        // Gini coefficient (0=equal, 1=total inequality)
    double hardship = 0.0;          // unmet basic needs (0=none, 1=severe)
    double development = 0.0;       // accumulated capital/infrastructure (0..5+)
    
    // Wealth distribution (for class formation)
    double wealth_top_10 = 0.0;     // share held by richest 10%
    double wealth_bottom_50 = 0.0;  // share held by poorest 50%
    
    // Economic system (emerges from agent beliefs + conditions)
    std::string economic_system = "mixed";  // "market", "planned", "mixed", "feudal", "cooperative"
    double system_stability = 1.0;  // how well system fits population beliefs
    
    // Tech multipliers (from Tech module, Phase 2.7)
    std::array<double, kGoodTypes> tech_multipliers = {1.0, 1.0, 1.0, 1.0, 1.0};
    
    // Institutional efficiency (from Institutions module, Phase 2.5)
    double efficiency = 1.0;
    
    // Trade connections (neighboring regions for trade)
    std::vector<std::uint32_t> trade_partners;
};

// Forward declaration
struct Agent;

class Economy {
public:
    ~Economy();
    void init(std::uint32_t num_regions,
              std::uint32_t num_agents,
              std::mt19937_64& rng,
              const std::string& start_condition);
    void update(const std::vector<std::uint32_t>& region_populations,
                const std::vector<std::array<double, 4>>& region_belief_centroids,
                const std::vector<Agent>& agents,
                std::uint64_t generation);
    
    // Accessors
    const RegionalEconomy& getRegion(std::uint32_t region_id) const;
    RegionalEconomy& getRegionMut(std::uint32_t region_id);
    
    AgentEconomy& getAgentEconomy(std::uint32_t agent_id);
    const AgentEconomy& getAgentEconomy(std::uint32_t agent_id) const;
    const std::vector<AgentEconomy>& agents() const { return agents_; }
    
    // Add a new agent to the economy (for births)
    void addAgent(std::uint32_t agent_id, std::uint32_t region_id, std::mt19937_64& rng);
    
    // Global metrics
    double globalWelfare() const;
    double globalInequality() const;
    double globalHardship() const;
    double globalDevelopment() const;
    
    // Trade analysis
    const std::vector<TradeLink>& getTradeLinks() const { return trade_links_; }
    double getTotalTrade() const;
    
    // Policy levers (for Phase 3+)
    void setEconomicModel(const std::string& model); // force a model globally
    
    // Resource allocation (for war module, Phase 2.8)
    void reallocateToWar(double fraction);
    
private:
    struct StartConditionProfile {
        std::string name;
        double baseDevelopment = 0.1;
        double developmentJitter = 0.05;
        std::array<double, kGoodTypes> endowmentMultipliers = {1.0, 1.0, 1.0, 1.0, 1.0};
        std::string defaultSystem = "mixed";
        double wealthLogMean = 0.0;
        double wealthLogStd = 0.7;
        double productivityMean = 1.0;
        double productivityStd = 0.3;
    };

    std::vector<RegionalEconomy> regions_;
    std::vector<TradeLink> trade_links_;
    std::vector<AgentEconomy> agents_;
    std::string forced_model_ = "";  // if set, overrides emergent systems
    double war_allocation_ = 0.0;
    std::string start_condition_name_ = "baseline";
    StartConditionProfile start_profile_{};
    
    // Matrix-based trade diffusion network
    std::unique_ptr<TradeNetwork> trade_network_;
    
    void initializeEndowments(std::mt19937_64& rng);
    void initializeTradeNetwork();
    void initializeAgents(std::uint32_t num_agents, std::mt19937_64& rng);
    
    void evolveSpecialization();
    void computeProduction();
    void computeTrade();
    void computeConsumption();
    void updatePrices();
    void distributeIncome(const std::vector<Agent>& agents);
    void computeWelfare();
    void computeInequality(const std::vector<Agent>& agents);
    void computeHardship();
    void evolveDevelopment();
    void evolveEconomicSystems(const std::vector<std::array<double, 4>>& region_belief_centroids);

    StartConditionProfile resolveStartCondition(const std::string& name) const;
    
    // Economic system emergence
    std::string determineEconomicSystem(const std::array<double, 4>& beliefs, 
                                       double development,
                                       double hardship,
                                       double inequality) const;
    
    // Wealth distribution
    double computeRegionGini(std::uint32_t region_id, const std::vector<Agent>& agents) const;
};

#endif
