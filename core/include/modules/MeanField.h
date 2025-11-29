#ifndef MEAN_FIELD_H
#define MEAN_FIELD_H

#include <array>
#include <vector>
#include <cstdint>

struct Agent;

/**
 * Neighbor influence accumulator for hybrid belief updates
 * Used to blend neighbor-based and regional field influence
 */
struct NeighborInfluence {
    std::array<double, 4> belief_sum{0.0, 0.0, 0.0, 0.0};
    double total_weight{0.0};
    int neighbor_count{0};
};

/**
 * Mean Field Approximation for Belief Updates
 * 
 * Replaces explicit neighbor iteration with regional "field" values.
 * 
 * Instead of:
 *   for each agent:
 *     for each neighbor:
 *       update belief based on neighbor
 * 
 * Use:
 *   for each region:
 *     compute regional field (average beliefs)
 *   for each agent:
 *     update belief based on regional field
 * 
 * Complexity drops from O(N·k) to O(N + R) ≈ O(N)
 * where N = agents, k = connections, R = regions
 */
class MeanFieldApproximation {
public:
    void configure(std::uint32_t num_regions);
    
    // Compute regional fields from agent population
    void computeFields(const std::vector<Agent>& agents,
                       const std::vector<std::vector<std::uint32_t>>& region_index);
    
    // Get field value for a region
    const std::array<double, 4>& getRegionalField(std::uint32_t region) const;
    
    // Get field strength (population-weighted influence)
    double getFieldStrength(std::uint32_t region) const;
    
    // Get blended influence combining neighbor and regional field
    // neighbor_weight: 0.0 = pure regional field, 1.0 = pure neighbor influence
    std::array<double, 4> getBlendedInfluence(
        const NeighborInfluence& neighbors,
        std::uint32_t region,
        double neighbor_weight = 0.6
    ) const;
    
    // Query
    const std::vector<std::array<double, 4>>& fields() const { return regional_fields_; }
    const std::vector<double>& strengths() const { return field_strengths_; }

private:
    std::uint32_t num_regions_ = 0;
    
    // Regional mean belief fields
    std::vector<std::array<double, 4>> regional_fields_;
    
    // Field strength (normalized by population density)
    std::vector<double> field_strengths_;
    
    // Population counts per region (for normalization)
    std::vector<std::uint32_t> region_populations_;
};

#endif
