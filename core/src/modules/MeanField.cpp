#include "modules/MeanField.h"
#include "kernel/Kernel.h"
#include <algorithm>
#include <cmath>

void MeanFieldApproximation::configure(std::uint32_t num_regions) {
    num_regions_ = num_regions;
    regional_fields_.assign(num_regions, {0.0, 0.0, 0.0, 0.0});
    field_strengths_.assign(num_regions, 1.0);
    region_populations_.assign(num_regions, 0);
}

void MeanFieldApproximation::computeFields(const std::vector<Agent>& agents,
                                           const std::vector<std::vector<std::uint32_t>>& region_index) {
    // Reset fields
    for (auto& field : regional_fields_) {
        field = {0.0, 0.0, 0.0, 0.0};
    }
    region_populations_.assign(num_regions_, 0);
    
    // Accumulate beliefs per region
    for (std::uint32_t r = 0; r < num_regions_ && r < region_index.size(); ++r) {
        const auto& agent_ids = region_index[r];
        
        for (auto agent_id : agent_ids) {
            if (agent_id >= agents.size()) continue;
            const auto& agent = agents[agent_id];
            if (!agent.alive) continue;
            
            regional_fields_[r][0] += agent.B[0];
            regional_fields_[r][1] += agent.B[1];
            regional_fields_[r][2] += agent.B[2];
            regional_fields_[r][3] += agent.B[3];
            region_populations_[r]++;
        }
    }
    
    // Compute averages and field strengths
    for (std::uint32_t r = 0; r < num_regions_; ++r) {
        if (region_populations_[r] > 0) {
            const double inv_pop = 1.0 / region_populations_[r];
            regional_fields_[r][0] *= inv_pop;
            regional_fields_[r][1] *= inv_pop;
            regional_fields_[r][2] *= inv_pop;
            regional_fields_[r][3] *= inv_pop;
            
            // Field strength: logarithmic scaling with population
            // Small groups have high variance, large groups have stable fields
            double pop = static_cast<double>(region_populations_[r]);
            field_strengths_[r] = std::min(1.0, std::log(pop + 1.0) / std::log(100.0));
        } else {
            // Empty region: neutral field
            regional_fields_[r] = {0.0, 0.0, 0.0, 0.0};
            field_strengths_[r] = 0.0;
        }
    }
}

const std::array<double, 4>& MeanFieldApproximation::getRegionalField(std::uint32_t region) const {
    static const std::array<double, 4> zero_field = {0.0, 0.0, 0.0, 0.0};
    if (region >= num_regions_) return zero_field;
    return regional_fields_[region];
}

double MeanFieldApproximation::getFieldStrength(std::uint32_t region) const {
    if (region >= num_regions_) return 0.0;
    return field_strengths_[region];
}
