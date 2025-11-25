#ifndef CLUSTERING_H
#define CLUSTERING_H

#include <array>
#include <vector>
#include <utility>
#include <cstdint>

// Forward declarations
class Kernel;
struct Agent;

struct Cluster {
    std::uint32_t id = 0;
    std::array<double, 4> centroid{0, 0, 0, 0};
    std::vector<std::uint32_t> members;
    double coherence = 0.0;
    std::array<double, 4> languageShare{0, 0, 0, 0};  // share per language family
    std::uint8_t dominantLang = 0;                     // most common language family
    std::uint8_t dominantDialect = 0;                  // most common dialect
    double linguisticHomogeneity = 0.0;                // how uniform is language
    std::vector<std::pair<std::uint32_t, double>> topRegions;
    std::uint64_t birthTick = 0;
    std::uint64_t deathTick = 0;
};

class KMeansClustering {
public:
    KMeansClustering(int k, int maxIter = 50, double tolerance = 1e-4);

    std::vector<Cluster> run(const Kernel& kernel);
    int iterationsUsed() const { return iterationsUsed_; }
    bool converged() const { return converged_; }

private:
    int k_;
    int maxIter_;
    double tolerance_;
    int iterationsUsed_ = 0;
    bool converged_ = false;

    static double distance(const std::array<double, 4>& a, const std::array<double, 4>& b);
    void initialize(const std::vector<Agent>& agents, std::vector<std::array<double, 4>>& centroids);
    void assign(const std::vector<Agent>& agents,
                const std::vector<std::array<double, 4>>& centroids,
                std::vector<int>& assignment);
    void update(const std::vector<Agent>& agents,
                const std::vector<int>& assignment,
                std::vector<std::array<double, 4>>& centroids);
    double inertia(const std::vector<Agent>& agents,
                   const std::vector<std::array<double, 4>>& centroids,
                   const std::vector<int>& assignment) const;
};

class DBSCANClustering {
public:
    DBSCANClustering(double eps = 0.3, int minPts = 50);

    std::vector<Cluster> run(const Kernel& kernel);
    int noisePoints() const { return noisePoints_; }

private:
    double eps_;
    int minPts_;
    int noisePoints_ = 0;

    static double distance(const std::array<double, 4>& a, const std::array<double, 4>& b);
    std::vector<std::uint32_t> regionQuery(const std::vector<Agent>& agents,
                                           std::uint32_t idx) const;
    void expandCluster(const std::vector<Agent>& agents,
                       std::uint32_t idx,
                       std::vector<std::uint32_t>& neighbors,
                       std::vector<int>& labels,
                       int clusterId);
};

struct ClusterMetrics {
    double withinVariance = 0.0;
    double betweenVariance = 0.0;
    double silhouette = 0.0;
    double diversity = 0.0;
};

ClusterMetrics computeClusterMetrics(const std::vector<Cluster>& clusters, const Kernel& kernel);
void enrichClusters(std::vector<Cluster>& clusters, const Kernel& kernel);

#endif
