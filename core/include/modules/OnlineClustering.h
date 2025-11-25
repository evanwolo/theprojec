#ifndef ONLINE_CLUSTERING_H
#define ONLINE_CLUSTERING_H

#include <array>
#include <vector>
#include <cstdint>

struct Agent;

/**
 * Online (Sequential) K-Means Clustering
 * 
 * Instead of batch "stop-the-world" clustering, this updates
 * cluster centroids incrementally as agents update beliefs.
 * 
 * Algorithm:
 *   1. Initialize K centroids (once)
 *   2. For each agent belief update:
 *      - Find nearest centroid
 *      - Update centroid: C_new = C_old + α(Agent - C_old)
 *   3. Periodically reassign agents to handle drift
 * 
 * Advantages:
 *   - No computational spikes (O(1) per update)
 *   - Always-current cluster state
 *   - Spreads cost evenly across simulation
 */
class OnlineClustering {
public:
    OnlineClustering(int k, double learning_rate = 0.01);
    
    // Initialize centroids from agent population
    void initialize(const std::vector<Agent>& agents);
    
    // Update a single agent's cluster assignment and centroid
    void updateAgent(std::uint32_t agent_id, const std::array<double, 4>& new_beliefs);
    
    // Periodic full reassignment (every N ticks to handle drift)
    void fullReassignment(const std::vector<Agent>& agents);
    
    // Query
    const std::vector<std::array<double, 4>>& centroids() const { return centroids_; }
    int getCluster(std::uint32_t agent_id) const;
    std::vector<std::uint32_t> getClusterMembers(int cluster_id) const;
    double getClusterCoherence(int cluster_id, const std::vector<Agent>& agents) const;
    
    // Statistics
    std::vector<std::uint32_t> getClusterSizes() const;
    double getTotalInertia(const std::vector<Agent>& agents) const;

private:
    int k_;
    double learning_rate_;
    
    // Cluster state
    std::vector<std::array<double, 4>> centroids_;
    std::vector<std::uint32_t> cluster_sizes_;  // Number of agents per cluster
    std::vector<int> assignments_;  // Agent ID → cluster ID mapping
    
    // Helpers
    int findNearestCentroid(const std::array<double, 4>& beliefs) const;
    double squaredDistance(const std::array<double, 4>& a, const std::array<double, 4>& b) const;
    void updateCentroid(int cluster_id, const std::array<double, 4>& agent_beliefs);
};

#endif
