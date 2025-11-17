#include <gtest/gtest.h>
#include "modules/Culture.h"

// Basic clustering test
TEST(ClusteringTest, KMeansClusters) {
    // Create test data: 100 agents with 4D beliefs
    std::vector<std::array<double, 4>> beliefs;
    beliefs.reserve(100);

    // Create two clear clusters
    for (int i = 0; i < 50; ++i) {
        beliefs.push_back({1.0, 0.0, 0.0, 0.0});  // Cluster 1
    }
    for (int i = 0; i < 50; ++i) {
        beliefs.push_back({0.0, 1.0, 0.0, 0.0});  // Cluster 2
    }

    // Run k-means clustering
    auto clusters = kmeans_clustering(beliefs, 2, 100);

    // Should find 2 clusters
    EXPECT_EQ(clusters.size(), 2);

    // Each cluster should have agents
    for (const auto& cluster : clusters) {
        EXPECT_GT(cluster.size(), 0);
    }
}

// DBSCAN clustering test
TEST(ClusteringTest, DBSCANClusters) {
    // Create test data with noise
    std::vector<std::array<double, 4>> beliefs;
    beliefs.reserve(60);

    // Dense cluster 1
    for (int i = 0; i < 30; ++i) {
        beliefs.push_back({1.0, 0.0, 0.0, 0.0});
    }

    // Dense cluster 2
    for (int i = 0; i < 20; ++i) {
        beliefs.push_back({0.0, 1.0, 0.0, 0.0});
    }

    // Noise points
    for (int i = 0; i < 10; ++i) {
        beliefs.push_back({0.5, 0.5, 0.5, 0.5});
    }

    // Run DBSCAN
    auto clusters = dbscan_clustering(beliefs, 0.1, 5);

    // Should find clusters (exact number depends on parameters)
    EXPECT_GT(clusters.size(), 0);
}