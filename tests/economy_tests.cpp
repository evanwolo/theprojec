#include <gtest/gtest.h>
#include "modules/Economy.h"
#include <random>

// Basic economy initialization test
TEST(EconomyTest, Initialization) {
    std::mt19937_64 rng(42);
    Economy economy(200, 10000, rng);

    // Check basic properties
    EXPECT_EQ(economy.getRegions().size(), 200);
    EXPECT_EQ(economy.getAgents().size(), 10000);
}

// Price bounds test - prices should stay within 0.1-10x range
TEST(EconomyTest, PriceBounds) {
    std::mt19937_64 rng(42);
    Economy economy(10, 500, rng);

    // Run several updates
    std::vector<std::uint32_t> regionPopulations(10, 50);
    std::vector<std::array<double, 4>> regionBeliefs(10, {0, 0, 0, 0});

    for (int i = 0; i < 100; ++i) {
        economy.update(regionPopulations, regionBeliefs, i);
    }

    // Check price bounds for all goods in all regions
    const auto& regions = economy.getRegions();
    for (const auto& region : regions) {
        for (int g = 0; g < 5; ++g) {
            EXPECT_GE(region.prices[g], 0.1) << "Price below minimum";
            EXPECT_LE(region.prices[g], 10.0) << "Price above maximum";
        }
    }
}

// Welfare computation test
TEST(EconomyTest, WelfareComputation) {
    std::mt19937_64 rng(42);
    Economy economy(5, 100, rng);

    std::vector<std::uint32_t> regionPopulations(5, 20);
    std::vector<std::array<double, 4>> regionBeliefs(5, {0, 0, 0, 0});

    // Run updates
    for (int i = 0; i < 10; ++i) {
        economy.update(regionPopulations, regionBeliefs, i);
    }

    double meanWelfare = economy.getMeanWelfare();
    double gini = economy.computeGini();

    // Sanity checks
    EXPECT_GE(meanWelfare, 0.0);
    EXPECT_GE(gini, 0.0);
    EXPECT_LE(gini, 1.0);
}
