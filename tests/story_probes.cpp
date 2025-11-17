#include <gtest/gtest.h>
#include "kernel/Kernel.h"
#include "modules/Economy.h"

// Story probe: Industrial crisis scenario
TEST(StoryProbeTest, IndustrialCrisis) {
    // Initialize kernel with 1000 agents
    Kernel kernel(1000);

    // Set up initial economic conditions
    // - High industrial capacity
    // - Low resource diversity
    // - Connected trade network

    // Run simulation for 1000 steps
    for (int step = 0; step < 1000; ++step) {
        kernel.step();
    }

    // Validate emergent behavior:
    // 1. Economic crisis should emerge
    // 2. Agents should adapt by diversifying production
    // 3. Trade networks should reconfigure

    auto metrics = kernel.get_metrics();

    // Check for crisis indicators
    EXPECT_LT(metrics.average_wealth, 100.0);  // Wealth should drop
    EXPECT_GT(metrics.diversity_index, 0.7);   // Diversity should increase
    EXPECT_GT(metrics.trade_volume, 50000);    // Trade should intensify
}

// Story probe: Regional specialization
TEST(StoryProbeTest, RegionalSpecialization) {
    Kernel kernel(2000);

    // Set up regional geography
    // - Coastal regions: fishing/trade focus
    // - Mountain regions: mining focus
    // - Plains: agriculture focus

    // Run long simulation
    for (int step = 0; step < 5000; ++step) {
        kernel.step();
    }

    auto metrics = kernel.get_metrics();

    // Check for specialization patterns
    EXPECT_GT(metrics.regional_diversity, 0.8);  // Regions should specialize
    EXPECT_LT(metrics.intra_regional_trade, 0.3); // Less internal trade
    EXPECT_GT(metrics.inter_regional_trade, 0.7); // More external trade
}

// Story probe: Belief evolution
TEST(StoryProbeTest, BeliefEvolution) {
    Kernel kernel(500);

    // Start with uniform beliefs
    // Run simulation with cultural pressure

    for (int step = 0; step < 2000; ++step) {
        kernel.step();
    }

    auto metrics = kernel.get_metrics();

    // Check for cultural clustering
    EXPECT_GT(metrics.cultural_clusters, 3);     // Multiple cultures emerge
    EXPECT_LT(metrics.belief_uniformity, 0.5);   // Beliefs diversify
    EXPECT_GT(metrics.cultural_stability, 0.9);  // Cultures stabilize
}