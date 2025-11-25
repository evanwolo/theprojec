#ifndef TRADE_NETWORK_H
#define TRADE_NETWORK_H

#include <vector>
#include <cstdint>
#include <array>

#include "modules/EconomyTypes.h"

/**
 * Matrix-based trade network using flow diffusion
 * 
 * Replaces pairwise trade loops with algebraic diffusion:
 *   Δq = -k(L · q)
 * 
 * where:
 *   - L is the Laplacian matrix (degree matrix - adjacency matrix)
 *   - q is the resource quantity vector
 *   - k is the diffusion coefficient
 *   - Δq is the change in quantities
 * 
 * This treats trade like heat/fluid flow through a network,
 * naturally balancing supply and demand through gradient descent.
 */
class TradeNetwork {
public:
    void configure(std::uint32_t num_regions);
    
    // Build adjacency from trade partner lists
    void buildTopology(const std::vector<std::vector<std::uint32_t>>& trade_partners);
    
    // Compute trade flows via matrix diffusion
    // Returns: trade_balance[region][good] = net exports (positive) or imports (negative)
    std::vector<std::array<double, kGoodTypes>> computeFlows(
        const std::vector<std::array<double, kGoodTypes>>& production,
        const std::vector<std::array<double, kGoodTypes>>& demand,
        const std::vector<std::uint32_t>& population,
        double diffusion_rate = 0.1
    );
    
    // Query
    const std::vector<std::vector<double>>& laplacian() const { return laplacian_; }
    std::uint32_t numRegions() const { return num_regions_; }

private:
    std::uint32_t num_regions_ = 0;
    
    // Laplacian matrix: L[i][j] = degree(i) if i==j, -1 if edge(i,j), else 0
    std::vector<std::vector<double>> laplacian_;
    
    // Adjacency for transport costs
    std::vector<std::vector<std::uint32_t>> adjacency_;
    
    // Helpers
    void computeLaplacian();
    void matrixVectorMultiply(const std::vector<double>& vec, 
                               std::vector<double>& result) const;
};

#endif
