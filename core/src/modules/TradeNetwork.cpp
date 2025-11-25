#include "modules/TradeNetwork.h"
#include "modules/Economy.h"
#include <algorithm>
#include <cmath>
#include <numeric>

void TradeNetwork::configure(std::uint32_t num_regions) {
    num_regions_ = num_regions;
    laplacian_.assign(num_regions, std::vector<double>(num_regions, 0.0));
    adjacency_.assign(num_regions, std::vector<std::uint32_t>());
}

void TradeNetwork::buildTopology(const std::vector<std::vector<std::uint32_t>>& trade_partners) {
    adjacency_.clear();
    adjacency_.resize(num_regions_);
    
    // Build adjacency from trade partner lists
    for (std::uint32_t i = 0; i < num_regions_ && i < trade_partners.size(); ++i) {
        adjacency_[i] = trade_partners[i];
    }
    
    computeLaplacian();
}

void TradeNetwork::computeLaplacian() {
    // Reset Laplacian
    for (auto& row : laplacian_) {
        std::fill(row.begin(), row.end(), 0.0);
    }
    
    // Laplacian = Degree matrix - Adjacency matrix
    for (std::uint32_t i = 0; i < num_regions_; ++i) {
        int degree = static_cast<int>(adjacency_[i].size());
        laplacian_[i][i] = static_cast<double>(degree);
        
        // Off-diagonal: -1 for each edge
        for (auto j : adjacency_[i]) {
            if (j < num_regions_) {
                laplacian_[i][j] = -1.0;
            }
        }
    }
}

void TradeNetwork::matrixVectorMultiply(const std::vector<double>& vec, 
                                         std::vector<double>& result) const {
    // result = L · vec
    result.assign(num_regions_, 0.0);
    
    for (std::uint32_t i = 0; i < num_regions_; ++i) {
        double sum = 0.0;
        for (std::uint32_t j = 0; j < num_regions_; ++j) {
            sum += laplacian_[i][j] * vec[j];
        }
        result[i] = sum;
    }
}

std::vector<std::array<double, kGoodTypes>> TradeNetwork::computeFlows(
    const std::vector<std::array<double, kGoodTypes>>& production,
    const std::vector<std::array<double, kGoodTypes>>& demand,
    const std::vector<std::uint32_t>& population,
    double diffusion_rate
) {
    std::vector<std::array<double, kGoodTypes>> trade_balance(num_regions_);
    
    // Initialize to zero
    for (auto& bal : trade_balance) {
        bal.fill(0.0);
    }
    
    // Process each good type independently
    for (int g = 0; g < kGoodTypes; ++g) {
        // Build surplus vector: q[i] = production[i][g] - demand[i][g]
        std::vector<double> surplus(num_regions_);
        for (std::uint32_t i = 0; i < num_regions_; ++i) {
            surplus[i] = production[i][g] - demand[i][g];
        }
        
        // Compute flow gradient: Δq = -k(L · surplus)
        std::vector<double> gradient;
        matrixVectorMultiply(surplus, gradient);
        
        // Apply diffusion: flow from high surplus to low surplus
        // Negative gradient means flow inward (imports)
        // Positive gradient means flow outward (exports)
        for (std::uint32_t i = 0; i < num_regions_; ++i) {
            // Flow = -k * gradient (negative sign makes flow go down gradient)
            double flow = -diffusion_rate * gradient[i];
            
            // Constrain flow to available surplus (can't export more than you have)
            if (flow > 0.0) {  // Exporting
                flow = std::min(flow, std::max(0.0, surplus[i]));
            } else {  // Importing
                // Limit imports by available exports in network
                // (This is implicitly handled by the flow conservation property of Laplacian)
                flow = std::max(flow, surplus[i]);
            }
            
            trade_balance[i][g] = flow;
        }
        
        // Normalize flows to ensure conservation (exports = imports globally)
        double total_flow = 0.0;
        for (std::uint32_t i = 0; i < num_regions_; ++i) {
            total_flow += trade_balance[i][g];
        }
        
        // If there's a net imbalance (numerical error), distribute it proportionally
        if (std::abs(total_flow) > 1e-6) {
            double correction = -total_flow / num_regions_;
            for (std::uint32_t i = 0; i < num_regions_; ++i) {
                trade_balance[i][g] += correction;
            }
        }
    }
    
    // Apply transport costs (reduce flow efficiency)
    constexpr double TRANSPORT_LOSS = 0.02;  // 2% loss per hop
    for (std::uint32_t i = 0; i < num_regions_; ++i) {
        int num_partners = static_cast<int>(adjacency_[i].size());
        if (num_partners == 0) continue;
        
        // Average transport cost based on network degree
        double transport_factor = 1.0 - TRANSPORT_LOSS * std::sqrt(num_partners);
        transport_factor = std::max(0.5, transport_factor);  // Cap at 50% loss
        
        for (int g = 0; g < kGoodTypes; ++g) {
            // Reduce absolute flow by transport costs
            double flow_magnitude = std::abs(trade_balance[i][g]);
            double sign = (trade_balance[i][g] >= 0.0) ? 1.0 : -1.0;
            trade_balance[i][g] = sign * flow_magnitude * transport_factor;
        }
    }
    
    return trade_balance;
}
