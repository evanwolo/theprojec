#include "kernel/Kernel.h"
#include "io/Snapshot.h"
#include "modules/Culture.h"
#include "modules/Economy.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <map>
#include <filesystem>

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
              << "  economy            # show economy summary\n"
              << "  region R           # show regional economy details\n"
              << "  classes            # show emergent economic classes\n"
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
                      << "Avg Conformity: " << m.avgConformity << "\n"
                      << "Global Welfare: " << m.globalWelfare << "\n"
                      << "Global Inequality: " << m.globalInequality << "\n"
                      << "Global Hardship: " << m.globalHardship << "\n";
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
            int ticks, log_freq;
            iss >> ticks >> log_freq;
            
            bool isNewFile = !std::filesystem::exists("metrics.csv");
            std::ofstream metricsFile("metrics.csv", std::ios::app);
            if (isNewFile) {
                metricsFile << "gen,welfare,inequality,hardship,polarization_mean,polarization_std,openness,conformity\n";
            }
            
            for (int t = 0; t < ticks; ++t) {
                kernel.step();
                if ((t + 1) % 100 == 0 || t == ticks - 1) {
                    std::cerr << "Tick " << (t + 1) << "/" << ticks << "\r";
                    std::cerr.flush();
                }
                if (t % log_freq == 0) {
                    auto m = kernel.computeMetrics();
                    metricsFile << kernel.generation() << ","
                                << m.globalWelfare << ","
                                << m.globalInequality << ","
                                << m.globalHardship << ","
                                << m.polarizationMean << ","
                                << m.polarizationStd << ","
                                << m.avgOpenness << ","
                                << m.avgConformity << "\n";
                }
            }
            
            std::cerr << "\n";
            metricsFile.close();
            std::cout << "Completed " << ticks << " ticks. Metrics written to data/metrics.csv\n";
            std::cout.flush();
            
        } else if (cmd == "economy") {
            const auto& econ = kernel.economy();
            std::cout << "\n=== Global Economy (Generation " << kernel.generation() << ") ===\n";
            std::cout << std::fixed << std::setprecision(3);
            std::cout << "Global Development: " << econ.globalDevelopment() << "\n";
            std::cout << "Total Trade Volume: " << econ.getTotalTrade() << "\n";
            std::cout << "Welfare: " << econ.globalWelfare() << "\n";
            std::cout << "Inequality (Gini): " << econ.globalInequality() << "\n";
            std::cout << "Hardship: " << econ.globalHardship() << "\n";
            
            // Count economic system types
            std::map<std::string, int> systemCounts;
            for (std::uint32_t r = 0; r < kernel.regionIndex().size(); ++r) {
                const auto& reg = econ.getRegion(r);
                if (reg.population > 0) {
                    systemCounts[reg.economic_system]++;
                }
            }
            std::cout << "\nEconomic Systems:\n";
            for (const auto& [system, count] : systemCounts) {
                std::cout << "  " << system << ": " << count << " regions\n";
            }
            std::cout << "\n";
            std::cout.flush();
            
        } else if (cmd == "region") {
            std::uint32_t rid;
            iss >> rid;
            if (rid >= kernel.regionIndex().size()) {
                std::cout << "Invalid region ID\n";
            } else {
                const auto& region = kernel.economy().getRegion(rid);
                std::cout << "\n=== Region " << rid << " Economy ===\n";
                std::cout << std::fixed << std::setprecision(3);
                std::cout << "Population: " << region.population << "\n";
                std::cout << "Economic System: " << region.economic_system << "\n";
                std::cout << "Development: " << region.development << "\n";
                std::cout << "Efficiency: " << region.efficiency << "\n\n";
                
                std::cout << "Production: Food=" << region.production[0] 
                         << ", Energy=" << region.production[1]
                         << ", Tools=" << region.production[2]
                         << ", Luxury=" << region.production[3]
                         << ", Services=" << region.production[4] << "\n";
                std::cout << "Specialization: Food=" << region.specialization[0] 
                         << ", Energy=" << region.specialization[1]
                         << ", Tools=" << region.specialization[2]
                         << ", Luxury=" << region.specialization[3]
                         << ", Services=" << region.specialization[4] << "\n";
                std::cout << "Consumption: Food=" << region.consumption[0] 
                         << ", Energy=" << region.consumption[1]
                         << ", Tools=" << region.consumption[2]
                         << ", Luxury=" << region.consumption[3]
                         << ", Services=" << region.consumption[4] << "\n";
                std::cout << "Prices: Food=" << region.prices[0] 
                         << ", Energy=" << region.prices[1]
                         << ", Tools=" << region.prices[2]
                         << ", Luxury=" << region.prices[3]
                         << ", Services=" << region.prices[4] << "\n\n";
                
                std::cout << "Welfare: " << region.welfare << "\n";
                std::cout << "Inequality: " << region.inequality << "\n";
                std::cout << "Hardship: " << region.hardship << "\n";
                std::cout << "Wealth Distribution: Top 10%=" << (region.wealth_top_10 * 100) << "%, Bottom 50%=" << (region.wealth_bottom_50 * 100) << "%\n\n";
            }
            std::cout.flush();
            
        } else if (cmd == "classes") {
            // Cluster agents by wealth percentile + sector to show emergent classes
            std::map<std::pair<int, int>, std::vector<std::uint32_t>> classes;
            const auto& agents_econ = kernel.economy().agents();
            
            // Compute wealth percentiles across all agents
            std::vector<double> all_wealths;
            all_wealths.reserve(agents_econ.size());
            for (const auto& ae : agents_econ) {
                all_wealths.push_back(ae.wealth);
            }
            std::sort(all_wealths.begin(), all_wealths.end());
            
            for (std::size_t agent_id = 0; agent_id < agents_econ.size(); ++agent_id) {
                const auto& ae = agents_econ[agent_id];
                // Find wealth decile (0-9)
                auto it = std::lower_bound(all_wealths.begin(), all_wealths.end(), ae.wealth);
                int wealth_decile = std::distance(all_wealths.begin(), it) * 10 / all_wealths.size();
                wealth_decile = std::min(9, wealth_decile); // cap at 9
                
                int sector = static_cast<int>(ae.sector);
                classes[{wealth_decile, sector}].push_back(agent_id);
            }
            
            std::cout << "\n=== Emergent Economic Classes ===\n";
            std::cout << "Format: Class(wealth_decile, sector): count agents\n";
            std::cout << "Sectors: 0=Food, 1=Energy, 2=Tools, 3=Luxury, 4=Services\n\n";
            
            for (const auto& [key, ids] : classes) {
                std::cout << "Class(" << key.first << "," << key.second << "): " 
                         << ids.size() << " agents\n";
            }
            std::cout << std::endl;
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
