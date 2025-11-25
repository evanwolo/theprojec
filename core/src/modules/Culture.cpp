#include "modules/Culture.h"
#include "kernel/Kernel.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <random>
#include <unordered_map>

namespace {

double distance4d(const std::array<double, 4>& a, const std::array<double, 4>& b) {
    double sum = 0.0;
    for (int i = 0; i < 4; ++i) {
        double d = a[i] - b[i];
        sum += d * d;
    }
    return std::sqrt(sum);
}

}

// ---------------- KMeans -----------------
KMeansClustering::KMeansClustering(int k, int maxIter, double tolerance)
    : k_(std::max(2, k)), maxIter_(std::max(1, maxIter)), tolerance_(std::max(1e-6, tolerance)) {}

void KMeansClustering::initialize(const std::vector<Agent>& agents,
                                  std::vector<std::array<double, 4>>& centroids) {
    centroids.clear();
    centroids.reserve(k_);

    std::mt19937_64 rng(agents.size());
    std::uniform_int_distribution<std::size_t> pick(0, agents.size() - 1);

    centroids.push_back(agents[pick(rng)].B);

    while (static_cast<int>(centroids.size()) < k_) {
        std::vector<double> minDists(agents.size(), std::numeric_limits<double>::max());
        for (std::size_t i = 0; i < agents.size(); ++i) {
            for (const auto& c : centroids) {
                // Use squared distance to avoid sqrt
                double d2 = 0.0;
                for (int dim = 0; dim < 4; ++dim) {
                    double diff = agents[i].B[dim] - c[dim];
                    d2 += diff * diff;
                }
                if (d2 < minDists[i]) minDists[i] = d2;
            }
        }
        std::discrete_distribution<std::size_t> dist(minDists.begin(), minDists.end());
        centroids.push_back(agents[dist(rng)].B);
    }
}

void KMeansClustering::assign(const std::vector<Agent>& agents,
                              const std::vector<std::array<double, 4>>& centroids,
                              std::vector<int>& assignment) {
    assignment.resize(agents.size());
    for (std::size_t i = 0; i < agents.size(); ++i) {
        double bestSq = std::numeric_limits<double>::max();
        int bestCluster = 0;
        // Compare squared distances to avoid sqrt
        for (int k = 0; k < k_; ++k) {
            double d2 = 0.0;
            for (int dim = 0; dim < 4; ++dim) {
                double diff = agents[i].B[dim] - centroids[k][dim];
                d2 += diff * diff;
            }
            if (d2 < bestSq) {
                bestSq = d2;
                bestCluster = k;
            }
        }
        assignment[i] = bestCluster;
    }
}

void KMeansClustering::update(const std::vector<Agent>& agents,
                              const std::vector<int>& assignment,
                              std::vector<std::array<double, 4>>& centroids) {
    std::vector<std::array<double, 4>> newC(k_, {0, 0, 0, 0});
    std::vector<int> counts(k_, 0);

    for (std::size_t i = 0; i < agents.size(); ++i) {
        int cluster = assignment[i];
        for (int d = 0; d < 4; ++d) {
            newC[cluster][d] += agents[i].B[d];
        }
        counts[cluster]++;
    }

    std::mt19937_64 rng(agents.size() * 7919);
    std::uniform_int_distribution<std::size_t> pick(0, agents.size() - 1);

    for (int k = 0; k < k_; ++k) {
        if (counts[k] == 0) {
            newC[k] = agents[pick(rng)].B;
        } else {
            for (int d = 0; d < 4; ++d) {
                newC[k][d] /= counts[k];
            }
        }
    }

    centroids = std::move(newC);
}

double KMeansClustering::inertia(const std::vector<Agent>& agents,
                                 const std::vector<std::array<double, 4>>& centroids,
                                 const std::vector<int>& assignment) const {
    double total = 0.0;
    for (std::size_t i = 0; i < agents.size(); ++i) {
        double d = distance4d(agents[i].B, centroids[assignment[i]]);
        total += d * d;
    }
    return total;
}

std::vector<Cluster> KMeansClustering::run(const Kernel& kernel) {
    const auto& agents = kernel.agents();
    std::vector<std::array<double, 4>> centroids;
    std::vector<int> assignment;

    initialize(agents, centroids);

    double prevInertia = std::numeric_limits<double>::max();
    converged_ = false;

    for (iterationsUsed_ = 0; iterationsUsed_ < maxIter_; ++iterationsUsed_) {
        assign(agents, centroids, assignment);
        update(agents, assignment, centroids);
        double current = inertia(agents, centroids, assignment);
        if (std::abs(prevInertia - current) < tolerance_) {
            converged_ = true;
            break;
        }
        prevInertia = current;
    }

    std::vector<Cluster> clusters(k_);
    for (int k = 0; k < k_; ++k) {
        clusters[k].id = static_cast<std::uint32_t>(k);
        clusters[k].centroid = centroids[k];
        clusters[k].birthTick = kernel.generation();
    }

    for (std::size_t i = 0; i < agents.size(); ++i) {
        clusters[assignment[i]].members.push_back(agents[i].id);
    }

    enrichClusters(clusters, kernel);
    return clusters;
}

// --------------- DBSCAN -----------------
DBSCANClustering::DBSCANClustering(double eps, int minPts)
    : eps_(std::max(1e-3, eps)), minPts_(std::max(2, minPts)) {}

std::vector<std::uint32_t> DBSCANClustering::regionQuery(const std::vector<Agent>& agents,
                                                         std::uint32_t idx) const {
    std::vector<std::uint32_t> neighbors;
    neighbors.reserve(minPts_ * 2);  // Reserve reasonable space
    const auto& point = agents[idx].B;
    const double eps2 = eps_ * eps_;  // Compare squared distances to avoid sqrt
    
    for (std::uint32_t i = 0; i < agents.size(); ++i) {
        // Compute squared distance
        double d2 = 0.0;
        for (int k = 0; k < 4; ++k) {
            double diff = point[k] - agents[i].B[k];
            d2 += diff * diff;
        }
        if (d2 <= eps2) {
            neighbors.push_back(i);
        }
    }
    return neighbors;
}

void DBSCANClustering::expandCluster(const std::vector<Agent>& agents,
                                     std::uint32_t idx,
                                     std::vector<std::uint32_t>& neighbors,
                                     std::vector<int>& labels,
                                     int clusterId) {
    labels[idx] = clusterId;
    std::size_t i = 0;
    while (i < neighbors.size()) {
        auto neighborIdx = neighbors[i];
        if (labels[neighborIdx] == -1) {
            labels[neighborIdx] = clusterId;
        }
        if (labels[neighborIdx] == 0) {
            labels[neighborIdx] = clusterId;
            auto neighborNeighbors = regionQuery(agents, neighborIdx);
            if (static_cast<int>(neighborNeighbors.size()) >= minPts_) {
                neighbors.insert(neighbors.end(), neighborNeighbors.begin(), neighborNeighbors.end());
            }
        }
        ++i;
    }
}

std::vector<Cluster> DBSCANClustering::run(const Kernel& kernel) {
    const auto& agents = kernel.agents();
    std::vector<int> labels(agents.size(), 0);
    int clusterId = 0;
    noisePoints_ = 0;

    for (std::uint32_t i = 0; i < agents.size(); ++i) {
        if (labels[i] != 0) continue;
        auto neighbors = regionQuery(agents, i);
        if (static_cast<int>(neighbors.size()) < minPts_) {
            labels[i] = -1;
            noisePoints_++;
        } else {
            ++clusterId;
            expandCluster(agents, i, neighbors, labels, clusterId);
        }
    }

    std::unordered_map<int, Cluster> map;
    for (std::uint32_t i = 0; i < agents.size(); ++i) {
        if (labels[i] <= 0) continue;
        int cid = labels[i] - 1;
        auto& cluster = map[cid];
        if (cluster.members.empty()) {
            cluster.id = static_cast<std::uint32_t>(cid);
            cluster.birthTick = kernel.generation();
        }
        cluster.members.push_back(agents[i].id);
    }

    std::vector<Cluster> clusters;
    clusters.reserve(map.size());
    for (auto& kv : map) {
        clusters.push_back(std::move(kv.second));
    }
    std::sort(clusters.begin(), clusters.end(), [](const Cluster& a, const Cluster& b) {
        return a.id < b.id;
    });

    enrichClusters(clusters, kernel);
    return clusters;
}

// ----------- Enrichment & Metrics --------

void enrichClusters(std::vector<Cluster>& clusters, const Kernel& kernel) {
    const auto& agents = kernel.agents();
    for (auto& cluster : clusters) {
        if (cluster.members.empty()) continue;
        std::array<double, 4> sum{0, 0, 0, 0};
        std::array<double, 4> sq{0, 0, 0, 0};
        std::array<int, 4> langs{0, 0, 0, 0};
        std::unordered_map<std::uint32_t, int> regionCounts;
        std::unordered_map<std::uint16_t, int> dialectCounts;  // key = lang*256 + dialect

        for (auto aid : cluster.members) {
            const auto& agent = agents[aid];
            for (int d = 0; d < 4; ++d) {
                sum[d] += agent.B[d];
                sq[d] += agent.B[d] * agent.B[d];
            }
            langs[agent.primaryLang]++;
            regionCounts[agent.region]++;
            // Track dialect distribution
            std::uint16_t dialectKey = static_cast<std::uint16_t>(agent.primaryLang) * 256 + agent.dialect;
            dialectCounts[dialectKey]++;
        }

        for (int d = 0; d < 4; ++d) {
            cluster.centroid[d] = sum[d] / cluster.members.size();
        }

        double variance = 0.0;
        for (int d = 0; d < 4; ++d) {
            double mean = cluster.centroid[d];
            variance += (sq[d] / cluster.members.size()) - mean * mean;
        }
        variance = std::max(0.0, variance / 4.0);
        cluster.coherence = std::max(0.0, 1.0 - variance);

        // Language family shares
        int maxLangCount = 0;
        for (int l = 0; l < 4; ++l) {
            cluster.languageShare[l] = static_cast<double>(langs[l]) / cluster.members.size();
            if (langs[l] > maxLangCount) {
                maxLangCount = langs[l];
                cluster.dominantLang = static_cast<std::uint8_t>(l);
            }
        }
        
        // Find dominant dialect
        int maxDialectCount = 0;
        for (const auto& [key, count] : dialectCounts) {
            if (count > maxDialectCount) {
                maxDialectCount = count;
                cluster.dominantDialect = static_cast<std::uint8_t>(key & 0xFF);
            }
        }
        
        // Linguistic homogeneity: how concentrated is the language distribution
        // 1.0 = everyone speaks same language, 0.25 = uniform across 4 languages
        double sumSqShare = 0.0;
        for (int l = 0; l < 4; ++l) {
            sumSqShare += cluster.languageShare[l] * cluster.languageShare[l];
        }
        // Normalize: (sumSq - 0.25) / 0.75 maps [0.25, 1.0] to [0.0, 1.0]
        cluster.linguisticHomogeneity = std::max(0.0, (sumSqShare - 0.25) / 0.75);

        std::vector<std::pair<std::uint32_t, int>> regionVec(regionCounts.begin(), regionCounts.end());
        std::sort(regionVec.begin(), regionVec.end(), [](const auto& a, const auto& b) {
            return a.second > b.second;
        });
        cluster.topRegions.clear();
        for (std::size_t i = 0; i < std::min<std::size_t>(5, regionVec.size()); ++i) {
            double share = static_cast<double>(regionVec[i].second) / cluster.members.size();
            cluster.topRegions.emplace_back(regionVec[i].first, share);
        }
    }
}

ClusterMetrics computeClusterMetrics(const std::vector<Cluster>& clusters, const Kernel& kernel) {
    ClusterMetrics metrics;
    const auto& agents = kernel.agents();
    if (agents.empty() || clusters.empty()) {
        return metrics;
    }

    double totalWithin = 0.0;
    for (const auto& cluster : clusters) {
        if (cluster.members.empty()) continue;
        for (auto aid : cluster.members) {
            totalWithin += std::pow(distance4d(agents[aid].B, cluster.centroid), 2);
        }
    }
    metrics.withinVariance = totalWithin / agents.size();

    std::array<double, 4> global{0, 0, 0, 0};
    for (const auto& agent : agents) {
        for (int d = 0; d < 4; ++d) {
            global[d] += agent.B[d];
        }
    }
    for (int d = 0; d < 4; ++d) {
        global[d] /= agents.size();
    }

    double between = 0.0;
    for (const auto& cluster : clusters) {
        if (cluster.members.empty()) continue;
        double weight = static_cast<double>(cluster.members.size()) / agents.size();
        between += weight * std::pow(distance4d(cluster.centroid, global), 2);
    }
    metrics.betweenVariance = between;

    double denom = std::max(metrics.withinVariance, metrics.betweenVariance);
    if (denom > 0) {
        metrics.silhouette = (metrics.betweenVariance - metrics.withinVariance) / denom;
    }

    double entropy = 0.0;
    for (const auto& cluster : clusters) {
        if (cluster.members.empty()) continue;
        double p = static_cast<double>(cluster.members.size()) / agents.size();
        entropy -= p * std::log2(std::max(p, 1e-12));
    }
    metrics.diversity = entropy;

    return metrics;
}
