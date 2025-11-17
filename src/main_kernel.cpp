#include "Kernel.h"
#include "KernelSnapshot.h"
#include "Clustering.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>

static void printHelp() {
    std::cerr << "Kernel Commands:\n"
              << "  step N             # advance N steps\n"
              << "  state [traits]     # print JSON snapshot (optional: include traits)\n"
              << "  metrics            # print current metrics\n"
              << "  reset [N R k p]    # reset with optional: pop, regions, k, rewire_p\n"
              << "  run T log          # run T ticks, log metrics every 'log' steps\n"
              << "  cluster kmeans K   # detect K cultures via K-means\n"
              << "  cluster dbscan e m # detect cultures via DBSCAN (eps, minPts)\n"
              << "  cultures           # print last detected cultures\n"
              << "  quit               # exit\n";
}

static void printClusters(const std::vector<Cluster>& clusters, const Kernel& kernel) {
    if (clusters.empty()) {
        std::cout << "No cultures detected. Run a 'cluster' command first.\n";
        return;
    }

    auto metrics = computeClusterMetrics(clusters, kernel);
    std::cout << std::fixed << std::setprecision(4);
    std::cout << "\n=== Cultural Clusters (generation " << kernel.generation() << ") ===\n";
    std::cout << "Total clusters: " << clusters.size() << "\n";
    std::cout << "Within variance: " << metrics.withinVariance << "\n";
    std::cout << "Between variance: " << metrics.betweenVariance << "\n";
    std::cout << "Silhouette: " << metrics.silhouette << "\n";
    std::cout << "Diversity: " << metrics.diversity << "\n\n";

    const char* langNames[] = {"Lang0", "Lang1", "Lang2", "Lang3"};

    for (const auto& cluster : clusters) {
        std::cout << std::setprecision(2)
                  << "Cluster " << cluster.id << " [" << cluster.members.size()
                  << " agents, coherence=" << cluster.coherence << "]\n";

        std::cout << "  Centroid: [" << std::setprecision(3);
        for (int d = 0; d < 4; ++d) {
            std::cout << cluster.centroid[d];
            if (d < 3) std::cout << ", ";
        }
        std::cout << "]\n";

        std::cout << "  Languages: ";
        bool anyLang = false;
        for (int l = 0; l < 4; ++l) {
            if (cluster.languageShare[l] > 0.01) {
                anyLang = true;
                std::cout << langNames[l] << "=" << std::setprecision(1)
                          << cluster.languageShare[l] * 100.0 << "% ";
            }
        }
        if (!anyLang) {
            std::cout << "n/a";
        }
        std::cout << "\n";

        std::cout << "  Top regions: ";
        if (cluster.topRegions.empty()) {
            std::cout << "n/a";
        } else {
            for (const auto& [region, frac] : cluster.topRegions) {
                std::cout << "R" << region << "=" << std::setprecision(1) << frac * 100.0 << "% ";
            }
        }
        std::cout << "\n\n";
    }
}

static std::vector<Cluster> g_lastClusters;

int main() {
    KernelConfig cfg;
    cfg.population = 50000;
    cfg.regions = 200;
    cfg.avgConnections = 8;
    cfg.rewireProb = 0.05;
    cfg.stepSize = 0.15;
    
    Kernel kernel(cfg);
    
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);
    
    printHelp();
    
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream iss(line);
        std::string cmd;
        if (!(iss >> cmd)) continue;
        
        if (cmd == "step") {
            int n = 1;
            iss >> n;
            if (n < 1) n = 1;
            for (int i = 0; i < n; ++i) {
                kernel.step();
                if ((i + 1) % 100 == 0 || i == n - 1) {
                    std::cerr << "Tick " << (i + 1) << "/" << n << "\r";
                    std::cerr.flush();
                }
            }
            std::cerr << "\n";
            std::cout << kernelToJson(kernel) << "\n";
            std::cout.flush();
            
                } else if (cmd == "cluster") {
                    std::string method;
                    iss >> method;
                    if (method == "kmeans") {
                        int k = 5;
                        iss >> k;
                        k = std::clamp(k, 2, 20);
                        std::cerr << "Running K-means with k=" << k << "...\n";
                        KMeansClustering km(k);
                        g_lastClusters = km.run(kernel);
                        std::cerr << "Iterations: " << km.iterationsUsed()
                                  << " (converged=" << (km.converged() ? "yes" : "no") << ")\n";
                        printClusters(g_lastClusters, kernel);
                    } else if (method == "dbscan") {
                        double eps = 0.3;
                        int minPts = 50;
                        iss >> eps >> minPts;
                        std::cerr << "Running DBSCAN (eps=" << eps << ", minPts=" << minPts << ")...\n";
                        DBSCANClustering db(eps, minPts);
                        g_lastClusters = db.run(kernel);
                        std::cerr << "Noise points: " << db.noisePoints() << "\n";
                        printClusters(g_lastClusters, kernel);
                    } else {
                        std::cerr << "Usage: cluster kmeans K | cluster dbscan eps minPts\n";
                    }
                } else if (cmd == "cultures") {
                    printClusters(g_lastClusters, kernel);
        } else if (cmd == "state") {
            std::string opt;
            iss >> opt;
            bool traits = (opt == "traits");
            std::cout << kernelToJson(kernel, traits) << "\n";
            std::cout.flush();
            
        } else if (cmd == "metrics") {
            auto m = kernel.computeMetrics();
            std::cout << "Generation: " << kernel.generation() << "\n"
                      << "Polarization: " << m.polarizationMean 
                      << " (±" << m.polarizationStd << ")\n"
                      << "Avg Openness: " << m.avgOpenness << "\n"
                      << "Avg Conformity: " << m.avgConformity << "\n";
            std::cout.flush();
            
        } else if (cmd == "reset") {
            std::uint32_t N = cfg.population;
            std::uint32_t R = cfg.regions;
            std::uint32_t k = cfg.avgConnections;
            double p = cfg.rewireProb;
            iss >> N >> R >> k >> p;
            
            KernelConfig newCfg = cfg;
            newCfg.population = N;
            newCfg.regions = R;
            newCfg.avgConnections = k;
            newCfg.rewireProb = p;
            
            kernel.reset(newCfg);
            g_lastClusters.clear();
            std::cout << "Reset: " << N << " agents, " << R << " regions\n";
            std::cout.flush();
            
        } else if (cmd == "run") {
            int T = 1000;
            int logEvery = 10;
            iss >> T >> logEvery;
            
            std::ofstream metricsFile("data/metrics.csv");
            metricsFile << "generation,polarization_mean,polarization_std,"
                       << "avg_openness,avg_conformity\n";
            
            for (int t = 0; t < T; ++t) {
                kernel.step();
                if ((t + 1) % 100 == 0 || t == T - 1) {
                    std::cerr << "Tick " << (t + 1) << "/" << T << "\r";
                    std::cerr.flush();
                }
                if (t % logEvery == 0) {
                    logMetrics(kernel, metricsFile);
                }
            }
            
            std::cerr << "\n";
            metricsFile.close();
            std::cout << "Completed " << T << " ticks. Metrics written to data/metrics.csv\n";
            std::cout.flush();
            
        } else if (cmd == "quit") {
            break;
            
        } else if (cmd == "help") {
            printHelp();
            
        } else {
            std::cerr << "Unknown command: " << cmd << "\n";
            printHelp();
        }
    }
    
    return 0;
}
