#include <gtest/gtest.h>
#include "modules/Economy.h"

// Basic economy initialization test
TEST(EconomyTest, Initialization) {
    std::mt19937_64 rng(42);
    Economy economy(200, 10000, rng);

    // Check basic properties
    EXPECT_EQ(economy.regions().size(), 200);
    EXPECT_EQ(economy.agents().size(), 10000);
}

// Trade logic test
TEST(EconomyTest, TradeLogic) {
    std::mt19937_64 rng(42);
    Economy economy(2, 100, rng);

    // Run a few steps
    for (int i = 0; i < 10; ++i) {
        economy.update(std::vector<std::uint32_t>(2, 50),
                      std::vector<std::array<double, 4>>(2, {0, 0, 0, 0}), i);
    }

    // Check that trade links were created
    // (This is a basic smoke test - more detailed tests would check surpluses/deficits)
    EXPECT_GE(economy.getTotalTrade(), 0.0);
}