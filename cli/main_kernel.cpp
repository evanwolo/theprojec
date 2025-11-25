#include "kernel/Kernel.h"
#include "io/Snapshot.h"
#include "modules/Culture.h"
#include "modules/Economy.h"
#ifdef HAS_GAME_MODULES
#include "modules/Movement.h"
#endif
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <map>
#include <filesystem>
#include <cstdlib>

static void printHelp() {
    std::cerr << "Kernel Commands:\n"
              << "  step N             # advance N steps\n"
              << "  state [traits]     # print JSON snapshot (optional: include traits)\n"
              << "  metrics            # print current metrics\n"
              << "  stats              # print detailed statistics (demographics, networks, beliefs)\n"
              << "  reset [N R k p]    # reset with optional: pop, regions, k, rewire_p\n"
              << "  run T log          # run T ticks, log metrics every 'log' steps\n"
              << "  cluster kmeans K   # detect K cultures via K-means\n"
              << "  cluster dbscan e m # detect cultures via DBSCAN (eps, minPts)\n"
              << "  cultures           # print last detected cultures\n"
              << "  economy            # show economy summary\n"
              << "  region R           # show regional economy details\n"
              << "  classes            # show emergent economic classes\n"
              << "  detect_movements   # detect movements from last clustering\n"
              << "  movements          # list active movements with stats\n"
              << "  movement ID        # show detailed info for movement ID\n"
              << "  quit               # exit\n"
              << "\nOptions: use --start=<profile> or SIM_START_CONDITION env var to choose economic start\n";
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

    // Language family names (can be customized)
    const char* langNames[] = {"Western", "Eastern", "Northern", "Southern"};

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

        // Show dominant language with dialect and linguistic homogeneity
        std::cout << "  Dominant language: " << langNames[cluster.dominantLang]
                  << " (dialect " << static_cast<int>(cluster.dominantDialect) << ")"
                  << ", homogeneity=" << std::setprecision(1) << cluster.linguisticHomogeneity * 100.0 << "%\n";

        std::cout << "  Language mix: ";
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

#ifdef HAS_GAME_MODULES
static MovementModule g_movements;
#endif

int main(int argc, char** argv) {
    KernelConfig cfg;
    cfg.population = 50000;
    cfg.regions = 200;
    cfg.avgConnections = 8;
    cfg.rewireProb = 0.05;
    cfg.stepSize = 0.15;
    cfg.demographyEnabled = true;  // Re-enable for debugging

    if (const char* envStart = std::getenv("SIM_START_CONDITION")) {
        cfg.startCondition = envStart;
    }

    const char* scriptArg = nullptr;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.rfind("--start=", 0) == 0) {
            cfg.startCondition = arg.substr(8);
        } else if (arg == "--help" || arg == "-h") {
            printHelp();
            return 0;
        } else if (arg.size() && arg[0] == '-' ) {
            std::cerr << "Unknown option: " << arg << "\n";
            return 1;
        } else {
            scriptArg = argv[i];
            break;
        }
    }
    
    Kernel kernel(cfg);
    
    // Check if there's a script file argument
    std::istream* input = &std::cin;
    std::ifstream scriptFile;
    
    if (scriptArg) {
        scriptFile.open(scriptArg);
        if (!scriptFile.is_open()) {
            std::cerr << "Error: Could not open script file '" << scriptArg << "'\n";
            return 1;
        }
        input = &scriptFile;
        std::cerr << "Running commands from script file: " << scriptArg << "\n";
    } else {
        // Interactive mode
        std::ios::sync_with_stdio(false);
        std::cin.tie(nullptr);
        printHelp();
    }
    
    std::string line;
    int lineCount = 0;
    while (std::getline(*input, line)) {
        lineCount++;
        std::cerr << "[DEBUG] Line " << lineCount << ": '" << line << "'\n";
        
        std::istringstream iss(line);
        std::string cmd;
        if (!(iss >> cmd)) {
            std::cerr << "[DEBUG] Empty line, skipping\n";
            continue;
        }
        std::cerr << "[DEBUG] Command: '" << cmd << "'\n";
        
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
            
        } else if (cmd == "stats") {
            try {
                auto stats = kernel.getStatistics();
                std::cout << "\n=== SIMULATION STATISTICS (Generation " << kernel.generation() << ") ===\n\n";
                
                if (stats.aliveAgents == 0) {
                    std::cout << "No alive agents!\n";
                    std::cout.flush();
                    continue;
                }
                
                // Population Overview
                std::cout << "--- POPULATION ---\n";
                std::cout << "Total agents: " << stats.totalAgents << "\n";
                std::cout << "Alive agents: " << stats.aliveAgents << "\n";
                std::cout << "Age range: " << stats.minAge << " - " << stats.maxAge << " years\n";
                std::cout << "Average age: " << std::fixed << std::setprecision(1) << stats.avgAge << " years\n\n";
            
            // Demographics by Age
            std::cout << "--- AGE DISTRIBUTION ---\n";
            std::cout << "Children (0-14):     " << std::setw(6) << stats.children 
                      << " (" << std::setprecision(1) << (100.0 * stats.children / stats.aliveAgents) << "%)\n";
            std::cout << "Young Adults (15-29): " << std::setw(6) << stats.youngAdults 
                      << " (" << std::setprecision(1) << (100.0 * stats.youngAdults / stats.aliveAgents) << "%)\n";
            std::cout << "Middle Age (30-49):  " << std::setw(6) << stats.middleAge 
                      << " (" << std::setprecision(1) << (100.0 * stats.middleAge / stats.aliveAgents) << "%)\n";
            std::cout << "Mature (50-69):      " << std::setw(6) << stats.mature 
                      << " (" << std::setprecision(1) << (100.0 * stats.mature / stats.aliveAgents) << "%)\n";
            std::cout << "Elderly (70+):       " << std::setw(6) << stats.elderly 
                      << " (" << std::setprecision(1) << (100.0 * stats.elderly / stats.aliveAgents) << "%)\n\n";
            
            // Gender
            std::cout << "--- GENDER ---\n";
            std::cout << "Males:   " << std::setw(6) << stats.males 
                      << " (" << std::setprecision(1) << (100.0 * stats.males / stats.aliveAgents) << "%)\n";
            std::cout << "Females: " << std::setw(6) << stats.females 
                      << " (" << std::setprecision(1) << (100.0 * stats.females / stats.aliveAgents) << "%)\n\n";
            
            // Network
            std::cout << "--- SOCIAL NETWORK ---\n";
            std::cout << "Avg connections: " << std::setprecision(2) << stats.avgConnections << "\n";
            std::cout << "Isolated agents: " << stats.isolatedAgents 
                      << " (" << std::setprecision(1) << (100.0 * stats.isolatedAgents / stats.aliveAgents) << "%)\n\n";
            
            // Beliefs
            std::cout << "--- BELIEFS ---\n";
            std::cout << "Polarization: " << std::setprecision(4) << stats.polarizationMean 
                      << " (±" << stats.polarizationStd << ")\n";
            std::cout << "Average beliefs:\n";
            std::cout << "  Authority-Liberty: " << std::setprecision(3) << stats.avgBeliefs[0] << "\n";
            std::cout << "  Tradition-Progress: " << stats.avgBeliefs[1] << "\n";
            std::cout << "  Hierarchy-Equality: " << stats.avgBeliefs[2] << "\n";
            std::cout << "  Isolation-Unity: " << stats.avgBeliefs[3] << "\n\n";
            
            // Regional Distribution
            std::cout << "--- REGIONAL DISTRIBUTION ---\n";
            std::cout << "Occupied regions: " << stats.occupiedRegions << " / " << cfg.regions << "\n";
            std::cout << "Avg population per region: " << std::setprecision(1) << stats.avgPopPerRegion << "\n";
            std::cout << "Min region population: " << stats.minRegionPop << "\n";
            std::cout << "Max region population: " << stats.maxRegionPop << "\n\n";
            
            // Economy
            std::cout << "--- ECONOMY ---\n";
            std::cout << "Global welfare: " << std::setprecision(3) << stats.globalWelfare << "\n";
            std::cout << "Global inequality: " << stats.globalInequality << "\n";
            std::cout << "Average income: " << std::setprecision(2) << stats.avgIncome << "\n\n";
            
            // Languages
            const char* langFamilies[] = {"Western", "Eastern", "Northern", "Southern"};
            std::cout << "--- LANGUAGES ---\n";
            std::cout << "Language families: " << static_cast<int>(stats.numLanguages) << "\n";
            for (int i = 0; i < 4; ++i) {
                if (stats.langCounts[i] > 0) {
                    std::cout << "  " << langFamilies[i] << ": " 
                              << stats.langCounts[i] << " speakers"
                              << " (" << std::setprecision(1) 
                              << (100.0 * stats.langCounts[i] / stats.aliveAgents) << "%)\n";
                }
            }
            std::cout << "  (Geographic zones: NW=Western, NE=Eastern, SW=Northern, SE=Southern)\n";
            std::cout << "\n";
            std::cout.flush();
            } catch (const std::exception& e) {
                std::cerr << "Error in stats command: " << e.what() << "\n";
            }
            
        } else if (cmd == "reset") {
            std::uint32_t N = cfg.population;
            std::uint32_t R = cfg.regions;
            std::uint32_t k = cfg.avgConnections;
            double p = cfg.rewireProb;
            iss >> N >> R >> k >> p;
            std::string startCond;
            if (iss >> startCond) {
                cfg.startCondition = startCond;
            }
            
            KernelConfig newCfg = cfg;
            newCfg.population = N;
            newCfg.regions = R;
            newCfg.avgConnections = k;
            newCfg.rewireProb = p;
            newCfg.startCondition = cfg.startCondition;
            
            kernel.reset(newCfg);
            g_lastClusters.clear();
            std::cout << "Reset: " << N << " agents, " << R << " regions (start="
                      << newCfg.startCondition << ")\n";
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
                if (t % log_freq == 0 || t == ticks - 1) {
                    auto m = kernel.computeMetrics();
                    
                    // Count alive agents
                    std::uint32_t alive_count = 0;
                    for (const auto& agent : kernel.agents()) {
                        if (agent.alive) alive_count++;
                    }
                    
                    // Write to CSV
                    metricsFile << kernel.generation() << ","
                                << m.globalWelfare << ","
                                << m.globalInequality << ","
                                << m.globalHardship << ","
                                << m.polarizationMean << ","
                                << m.polarizationStd << ","
                                << m.avgOpenness << ","
                                << m.avgConformity << "\n";
                    
                    // Print tick summary to stdout
                    std::cout << "Tick " << (t + 1) << ": "
                              << "Pop=" << alive_count << ", "
                              << "Pol=" << std::fixed << std::setprecision(3) << m.polarizationMean << ", "
                              << "Welfare=" << m.globalWelfare << ", "
                              << "Ineq=" << m.globalInequality << ", "
                              << "Hard=" << m.globalHardship << ", "
                              << "Trade=" << static_cast<int>(kernel.economy().getTotalTrade())
                              << "\n";
                    std::cout.flush();
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
                std::cout << "\n=== Region " << rid << " ===\n";
                std::cout << std::fixed << std::setprecision(3);
                
                // Geographic info
                std::cout << "Location: (" << region.x << ", " << region.y << ")";
                // Determine quadrant/language zone
                const char* quadrant;
                if (region.x < 0.5 && region.y >= 0.5) quadrant = "Northwest (Western)";
                else if (region.x >= 0.5 && region.y >= 0.5) quadrant = "Northeast (Eastern)";
                else if (region.x < 0.5 && region.y < 0.5) quadrant = "Southwest (Northern)";
                else quadrant = "Southeast (Southern)";
                std::cout << " - " << quadrant << "\n\n";
                
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
            
        } else if (cmd == "detect_movements") {
#ifdef HAS_GAME_MODULES
            if (g_lastClusters.empty()) {
                std::cerr << "No clusters detected. Run 'cluster kmeans K' or 'cluster dbscan' first.\n";
                continue;
            }
            
            std::cerr << "Detecting movements from " << g_lastClusters.size() << " clusters...\n";
            g_movements.update(kernel, g_lastClusters, kernel.generation());
            
            auto stats = g_movements.computeStats();
            std::cout << "Detected " << stats.totalMovements << " movements (" 
                      << stats.totalMembership << " total members)\n";
            std::cout.flush();
#else
            std::cerr << "Movement module not available (built without HAS_GAME_MODULES)\n";
#endif
            
        } else if (cmd == "movements") {
#ifdef HAS_GAME_MODULES
            auto stats = g_movements.computeStats();
            
            std::cout << "\n=== Active Movements (Generation " << kernel.generation() << ") ===\n";
            std::cout << "Total movements: " << stats.totalMovements << "\n";
            std::cout << "Total membership: " << stats.totalMembership << " agents\n";
            std::cout << "Average power: " << std::fixed << std::setprecision(3) << stats.avgPower << "\n";
            std::cout << "Average size: " << std::fixed << std::setprecision(1) << stats.avgSize << "\n";
            std::cout << "Stages: Birth=" << stats.birthStage << " Growth=" << stats.growthStage
                      << " Plateau=" << stats.plateauStage << " Decline=" << stats.declineStage << "\n\n";
            
            auto byPower = g_movements.movementsByPower();
            const char* stageNames[] = {"Birth", "Growth", "Plateau", "Schism", "Decline", "Dead"};
            
            for (const auto* mov : byPower) {
                std::cout << "Movement #" << mov->id << " [" << stageNames[static_cast<int>(mov->stage)] << "]\n";
                std::cout << "  Size: " << mov->members.size() << " | Power: " << std::fixed << std::setprecision(3) << mov->power << "\n";
                std::cout << "  Platform: [" << std::setprecision(2);
                for (int d = 0; d < 4; ++d) {
                    std::cout << mov->platform[d];
                    if (d < 3) std::cout << ", ";
                }
                std::cout << "]\n";
                std::cout << "  Coherence: " << std::setprecision(3) << mov->coherence
                          << " | Street: " << mov->streetCapacity
                          << " | Charisma: " << mov->charismaScore << "\n";
                
                if (!mov->regionalStrength.empty()) {
                    std::cout << "  Top regions: ";
                    std::vector<std::pair<std::uint32_t, double>> sortedRegions(
                        mov->regionalStrength.begin(), mov->regionalStrength.end());
                    std::sort(sortedRegions.begin(), sortedRegions.end(),
                              [](const auto& a, const auto& b) { return a.second > b.second; });
                    int shown = 0;
                    for (const auto& [rid, strength] : sortedRegions) {
                        if (shown >= 3) break;
                        std::cout << "R" << rid << "=" << std::setprecision(1) << (strength * 100.0) << "% ";
                        shown++;
                    }
                    std::cout << "\n";
                }
                std::cout << "\n";
            }
            std::cout.flush();
#else
            std::cerr << "Movement module not available (built without HAS_GAME_MODULES)\n";
#endif
            
        } else if (cmd == "movement") {
#ifdef HAS_GAME_MODULES
            std::uint32_t movId;
            if (!(iss >> movId)) {
                std::cerr << "Usage: movement ID\n";
                continue;
            }
            
            auto* mov = g_movements.findMovement(movId);
            if (!mov) {
                std::cerr << "Movement #" << movId << " not found.\n";
                continue;
            }
            
            const char* stageNames[] = {"Birth", "Growth", "Plateau", "Schism", "Decline", "Dead"};
            std::cout << "\n=== Movement #" << mov->id << " ===\n";
            std::cout << "Stage: " << stageNames[static_cast<int>(mov->stage)] << "\n";
            std::cout << "Birth: tick " << mov->birthTick << " | Last update: " << mov->lastUpdateTick << "\n";
            std::cout << "Size: " << mov->members.size() << " agents\n";
            std::cout << "Leaders: " << mov->leaders.size() << "\n";
            std::cout << "Power: " << std::fixed << std::setprecision(3) << mov->power << "\n";
            std::cout << "  Street capacity: " << mov->streetCapacity << "\n";
            std::cout << "  Charisma score: " << mov->charismaScore << "\n";
            std::cout << "Coherence: " << mov->coherence << "\n";
            std::cout << "Momentum: " << mov->momentum << "\n";
            std::cout << "Platform: [" << std::setprecision(2);
            for (int d = 0; d < 4; ++d) {
                std::cout << mov->platform[d];
                if (d < 3) std::cout << ", ";
            }
            std::cout << "]\n";
            
            if (!mov->classComposition.empty()) {
                std::cout << "Class composition:\n";
                for (const auto& [decile, proportion] : mov->classComposition) {
                    std::cout << "  Decile " << decile << ": " << std::setprecision(1) << (proportion * 100.0) << "%\n";
                }
            }
            
            if (!mov->regionalStrength.empty()) {
                std::cout << "Regional strength:\n";
                std::vector<std::pair<std::uint32_t, double>> sortedRegions(
                    mov->regionalStrength.begin(), mov->regionalStrength.end());
                std::sort(sortedRegions.begin(), sortedRegions.end(),
                          [](const auto& a, const auto& b) { return a.second > b.second; });
                for (const auto& [rid, strength] : sortedRegions) {
                    std::cout << "  Region " << rid << ": " << std::setprecision(1) << (strength * 100.0) << "%\n";
                }
            }
            std::cout.flush();
#else
            std::cerr << "Movement module not available (built without HAS_GAME_MODULES)\n";
#endif
            
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


