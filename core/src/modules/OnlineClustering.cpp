#include "modules/OnlineClustering.h"
#include "kernel/Kernel.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <random>

OnlineClustering::OnlineClustering(int k, double learning_rate)
    : k_(std::max(2, k)), learning_rate_(learning_rate) {
    centroids_.resize(k_);
    cluster_sizes_.assign(k_, 0);
}

void OnlineClustering::initialize(const std::vector<Agent>& agents) {
    if (agents.empty()) return;
    
    // K-means++ initialization for good starting centroids
    std::mt19937_64 rng(agents.size());
    std::uniform_int_distribution<std::size_t> pick(0, agents.size() - 1);
    
    // First centroid: random agent
    centroids_[0] = agents[pick(rng)].B;
    
    // Remaining centroids: weighted by distance to nearest existing centroid
    for (int c = 1; c < k_; ++c) {
        std::vector<double> min_distances(agents.size(), std::numeric_limits<double>::max());
        
        for (std::size_t i = 0; i < agents.size(); ++i) {
            for (int existing = 0; existing < c; ++existing) {
                double dist = squaredDistance(agents[i].B, centroids_[existing]);
                min_distances[i] = std::min(min_distances[i], dist);
            }
        }
        
        // Choose next centroid with probability proportional to distance squared
        std::discrete_distribution<std::size_t> dist(min_distances.begin(), min_distances.end());
        centroids_[c] = agents[dist(rng)].B;
    }
    
    // Initial assignment
    assignments_.assign(agents.size(), -1);
    cluster_sizes_.assign(k_, 0);
    
    for (std::size_t i = 0; i < agents.size(); ++i) {
        int cluster = findNearestCentroid(agents[i].B);
        assignments_[i] = cluster;
        cluster_sizes_[cluster]++;
    }
}

int OnlineClustering::findNearestCentroid(const std::array<double, 4>& beliefs) const {
    double best_dist = std::numeric_limits<double>::max();
    int best_cluster = 0;
    
    for (int c = 0; c < k_; ++c) {
        double dist = squaredDistance(beliefs, centroids_[c]);
        if (dist < best_dist) {
            best_dist = dist;
            best_cluster = c;
        }
    }
    
    return best_cluster;
}

double OnlineClustering::squaredDistance(const std::array<double, 4>& a, 
                                         const std::array<double, 4>& b) const {
    double sum = 0.0;
    for (int d = 0; d < 4; ++d) {
        double diff = a[d] - b[d];
        sum += diff * diff;
    }
    return sum;
}

void OnlineClustering::updateAgent(std::uint32_t agent_id, const std::array<double, 4>& new_beliefs) {
    // Ensure assignments vector is large enough
    if (agent_id >= assignments_.size()) {
        assignments_.resize(agent_id + 1, -1);
    }
    
    // Find nearest centroid for new beliefs
    int new_cluster = findNearestCentroid(new_beliefs);
    int old_cluster = assignments_[agent_id];
    
    // If cluster changed, update counts
    if (old_cluster != new_cluster && old_cluster >= 0) {
        cluster_sizes_[old_cluster] = (cluster_sizes_[old_cluster] > 0) ? 
                                       (cluster_sizes_[old_cluster] - 1) : 0;
        cluster_sizes_[new_cluster]++;
        assignments_[agent_id] = new_cluster;
    } else if (old_cluster < 0) {
        // First assignment
        cluster_sizes_[new_cluster]++;
        assignments_[agent_id] = new_cluster;
    }
    
    // Update centroid incrementally: C_new = C_old + α(Agent - C_old)
    updateCentroid(new_cluster, new_beliefs);
}

void OnlineClustering::updateCentroid(int cluster_id, const std::array<double, 4>& agent_beliefs) {
    if (cluster_id < 0 || cluster_id >= k_) return;
    
    // Adaptive learning rate based on cluster size
    // Larger clusters → slower adaptation (more stable)
    // Smaller clusters → faster adaptation (more responsive)
    double adaptive_rate = learning_rate_;
    if (cluster_sizes_[cluster_id] > 0) {
        // Use logarithmic decay, and cap denominator to avoid vanishing rates
        double denom = std::max(1.0, std::log(static_cast<double>(cluster_sizes_[cluster_id]) + 1.0));
        adaptive_rate = learning_rate_ / denom;
    }
    adaptive_rate = std::min(0.1, adaptive_rate);  // Cap at 10% per update
    
    // Incremental update
    for (int d = 0; d < 4; ++d) {
        centroids_[cluster_id][d] += adaptive_rate * (agent_beliefs[d] - centroids_[cluster_id][d]);
    }
}

void OnlineClustering::fullReassignment(const std::vector<Agent>& agents) {
    // Reset counts
    cluster_sizes_.assign(k_, 0);
    
    // Ensure assignments is large enough
    if (assignments_.size() < agents.size()) {
        assignments_.resize(agents.size(), -1);
    }
    
    // Reassign all agents
    for (std::size_t i = 0; i < agents.size(); ++i) {
        if (!agents[i].alive) continue;
        
        int cluster = findNearestCentroid(agents[i].B);
        assignments_[i] = cluster;
        cluster_sizes_[cluster]++;
    }
    
    // Recompute centroids from scratch (stabilization step)
    std::vector<std::array<double, 4>> new_centroids(k_, {0.0, 0.0, 0.0, 0.0});
    
    for (std::size_t i = 0; i < agents.size(); ++i) {
        if (!agents[i].alive) continue;
        
        int cluster = assignments_[i];
        if (cluster >= 0 && cluster < k_) {
            for (int d = 0; d < 4; ++d) {
                new_centroids[cluster][d] += agents[i].B[d];
            }
        }
    }
    
    // Average and handle empty clusters
    std::mt19937_64 rng(agents.size() * 7919);
    std::uniform_int_distribution<std::size_t> pick(0, agents.size() - 1);
    
    for (int c = 0; c < k_; ++c) {
        if (cluster_sizes_[c] == 0) {
            // Reinitialize empty cluster to random agent
            new_centroids[c] = agents[pick(rng)].B;
        } else {
            for (int d = 0; d < 4; ++d) {
                new_centroids[c][d] /= cluster_sizes_[c];
            }
        }
    }
    
    centroids_ = std::move(new_centroids);
}

int OnlineClustering::getCluster(std::uint32_t agent_id) const {
    if (agent_id >= assignments_.size()) return -1;
    return assignments_[agent_id];
}

std::vector<std::uint32_t> OnlineClustering::getClusterMembers(int cluster_id) const {
    std::vector<std::uint32_t> members;
    
    for (std::size_t i = 0; i < assignments_.size(); ++i) {
        if (assignments_[i] == cluster_id) {
            members.push_back(static_cast<std::uint32_t>(i));
        }
    }
    
    return members;
}

double OnlineClustering::getClusterCoherence(int cluster_id, const std::vector<Agent>& agents) const {
    if (cluster_id < 0 || cluster_id >= k_) return 0.0;
    
    auto members = getClusterMembers(cluster_id);
    if (members.empty()) return 0.0;
    
    // Compute average distance to centroid
    double total_dist = 0.0;
    for (auto agent_id : members) {
        if (agent_id < agents.size() && agents[agent_id].alive) {
            total_dist += std::sqrt(squaredDistance(agents[agent_id].B, centroids_[cluster_id]));
        }
    }
    
    double avg_dist = total_dist / members.size();
    
    // Convert to coherence (1 = perfect, 0 = dispersed)
    // Assume distance range [0, 4] (max distance in 4D [-1,1]^4 space)
    return std::max(0.0, 1.0 - (avg_dist / 4.0));
}

std::vector<std::uint32_t> OnlineClustering::getClusterSizes() const {
    return cluster_sizes_;
}

double OnlineClustering::getTotalInertia(const std::vector<Agent>& agents) const {
    double total = 0.0;
    
    for (std::size_t i = 0; i < agents.size(); ++i) {
        if (!agents[i].alive) continue;
        
        int cluster = (i < assignments_.size()) ? assignments_[i] : -1;
        if (cluster >= 0 && cluster < k_) {
            total += squaredDistance(agents[i].B, centroids_[cluster]);
        }
    }
    
    return total;
}
