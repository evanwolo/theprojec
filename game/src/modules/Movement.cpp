#include "modules/Movement.h"
#include "kernel/Kernel.h"
#include "modules/Culture.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <set>

MovementModule::MovementModule(const MovementFormationConfig& cfg) : cfg_(cfg) {}

// Main update: detect new formations, update existing movements
void MovementModule::update(Kernel& kernel, const std::vector<Cluster>& clusters, std::uint64_t tick) {
    detectFormations(kernel, clusters, tick);
    updateExistingMovements(kernel, tick);
    pruneDeadMovements();
}

// Formation detection from clusters
void MovementModule::detectFormations(Kernel& kernel, const std::vector<Cluster>& clusters, std::uint64_t tick) {
    for (const auto& cluster : clusters) {
        if (shouldFormMovement(cluster, kernel)) {
            movements_.push_back(createMovement(cluster, kernel, tick));
        }
    }
}

bool MovementModule::shouldFormMovement(const Cluster& cluster, const Kernel& kernel) const {
    // Size check
    if (cluster.members.size() < cfg_.minSize) {
        return false;
    }
    
    // Coherence check
    if (cluster.coherence < cfg_.minCoherence) {
        return false;
    }
    
    // Charisma density check: proportion of high-assertiveness agents
    const auto& agents = kernel.agents();
    int charismaticCount = 0;
    for (auto agentId : cluster.members) {
        if (agents[agentId].assertiveness > 0.7) {
            charismaticCount++;
        }
    }
    double charismaDensity = static_cast<double>(charismaticCount) / cluster.members.size();
    if (charismaDensity < cfg_.minCharismaDensity) {
        return false;
    }
    
    // Economic hardship check (optional trigger)
    const auto& economy = kernel.economy();
    const auto& ecoAgents = economy.agents();
    double avgHardship = 0.0;
    for (auto agentId : cluster.members) {
        if (agentId < ecoAgents.size()) {
            avgHardship += ecoAgents[agentId].hardship;
        }
    }
    avgHardship /= cluster.members.size();
    
    // Form if hardship is high OR coherence is very high
    if (avgHardship > cfg_.hardshipThreshold || cluster.coherence > 0.85) {
        return true;
    }
    
    return false;
}

Movement MovementModule::createMovement(const Cluster& cluster, const Kernel& kernel, std::uint64_t tick) {
    Movement mov;
    mov.id = nextId_++;
    mov.birthTick = tick;
    mov.lastUpdateTick = tick;
    mov.stage = MovementStage::Birth;
    
    // Platform = cluster centroid
    mov.platform = cluster.centroid;
    
    // Members
    mov.members = cluster.members;
    
    // Identify leaders
    mov.leaders = identifyLeaders(cluster.members, kernel, 5);
    
    // Coherence from cluster
    mov.coherence = cluster.coherence;
    
    // Initial metrics
    updateMembership(mov, kernel);
    updatePowerMetrics(mov, kernel);
    
    return mov;
}

// Update existing movements
void MovementModule::updateExistingMovements(Kernel& kernel, std::uint64_t tick) {
    for (auto& mov : movements_) {
        if (mov.stage == MovementStage::Dead) continue;
        
        updateMembership(mov, kernel);
        updatePowerMetrics(mov, kernel);
        updateStage(mov);
        
        mov.lastUpdateTick = tick;
    }
}

void MovementModule::updateMembership(Movement& mov, const Kernel& kernel) {
    const auto& agents = kernel.agents();
    
    // Recompute platform as mean of current members
    std::array<double, 4> newPlatform{0, 0, 0, 0};
    for (auto agentId : mov.members) {
        const auto& agent = agents[agentId];
        for (int d = 0; d < 4; ++d) {
            newPlatform[d] += agent.B[d];
        }
    }
    if (!mov.members.empty()) {
        for (int d = 0; d < 4; ++d) {
            newPlatform[d] /= mov.members.size();
        }
    }
    mov.platform = newPlatform;
    
    // Recompute coherence (variance in belief space)
    double variance = 0.0;
    for (auto agentId : mov.members) {
        const auto& agent = agents[agentId];
        double dist = 0.0;
        for (int d = 0; d < 4; ++d) {
            double diff = agent.B[d] - mov.platform[d];
            dist += diff * diff;
        }
        variance += std::sqrt(dist);
    }
    if (!mov.members.empty()) {
        variance /= mov.members.size();
    }
    mov.coherence = std::max(0.0, 1.0 - variance);
    
    // Regional strength
    mov.regionalStrength.clear();
    const auto& regionIndex = kernel.regionIndex();
    for (std::uint32_t r = 0; r < regionIndex.size(); ++r) {
        int count = 0;
        for (auto agentId : mov.members) {
            if (agents[agentId].region == r) {
                count++;
            }
        }
        if (count > 0) {
            mov.regionalStrength[r] = static_cast<double>(count) / mov.members.size();
        }
    }
    
    // Class composition (wealth deciles)
    mov.classComposition.clear();
    const auto& economy = kernel.economy();
    const auto& ecoAgents = economy.agents();
    
    // Collect all agent wealths for proper decile calculation
    std::vector<double> all_wealths;
    all_wealths.reserve(ecoAgents.size());
    for (const auto& ae : ecoAgents) {
        all_wealths.push_back(ae.wealth);
    }
    std::sort(all_wealths.begin(), all_wealths.end());
    
    // Calculate deciles for movement members based on global wealth distribution
    for (auto agentId : mov.members) {
        if (agentId < ecoAgents.size()) {
            double wealth = ecoAgents[agentId].wealth;
            // Find position in sorted wealth distribution
            auto it = std::lower_bound(all_wealths.begin(), all_wealths.end(), wealth);
            int decile = std::distance(all_wealths.begin(), it) * 10 / all_wealths.size();
            decile = std::min(9, decile);  // cap at 9
            mov.classComposition[decile]++;
        }
    }
    // Normalize
    for (auto& [decile, count] : mov.classComposition) {
        count /= mov.members.size();
    }
}

void MovementModule::updatePowerMetrics(Movement& mov, const Kernel& kernel) {
    const auto& agents = kernel.agents();
    const auto& economy = kernel.economy();
    const auto& ecoAgents = economy.agents();
    
    // Street capacity: sum of assertiveness * (1 + hardship)
    double streetPower = 0.0;
    for (auto agentId : mov.members) {
        double assertiveness = agents[agentId].assertiveness;
        double hardship = (agentId < ecoAgents.size()) ? ecoAgents[agentId].hardship : 0.0;
        streetPower += assertiveness * (1.0 + hardship);
    }
    mov.streetCapacity = streetPower / (mov.members.size() + 1.0);
    
    // Charisma score: average assertiveness of leaders
    double charismaSum = 0.0;
    for (auto leaderId : mov.leaders) {
        charismaSum += agents[leaderId].assertiveness;
    }
    mov.charismaScore = mov.leaders.empty() ? 0.0 : charismaSum / mov.leaders.size();
    
    // Power: weighted sum of metrics
    mov.power = 0.5 * mov.streetCapacity +
                0.3 * mov.coherence +
                0.2 * mov.charismaScore;
    mov.power = std::clamp(mov.power, 0.0, 1.0);
}

void MovementModule::updateStage(Movement& mov) {
    std::uint64_t age = mov.lastUpdateTick - mov.birthTick;
    
    // Simple stage progression based on size and coherence
    if (mov.coherence < 0.3) {
        mov.stage = MovementStage::Decline;
    } else if (mov.members.size() < 50) {
        mov.stage = MovementStage::Decline;
    } else if (age < 100) {
        mov.stage = MovementStage::Birth;
    } else if (mov.momentum > 0.01) {
        mov.stage = MovementStage::Growth;
    } else if (mov.momentum < -0.01) {
        mov.stage = MovementStage::Decline;
    } else {
        mov.stage = MovementStage::Plateau;
    }
    
    // Death condition
    if (mov.members.size() < 20 || mov.coherence < 0.2) {
        mov.stage = MovementStage::Dead;
    }
}

void MovementModule::pruneDeadMovements() {
    movements_.erase(
        std::remove_if(movements_.begin(), movements_.end(),
                       [](const Movement& m) { return m.stage == MovementStage::Dead; }),
        movements_.end()
    );
}

// Identify top-N leaders by assertiveness
std::vector<std::uint32_t> MovementModule::identifyLeaders(const std::vector<std::uint32_t>& members,
                                                             const Kernel& kernel,
                                                             std::uint32_t topN) const {
    const auto& agents = kernel.agents();
    std::vector<std::pair<std::uint32_t, double>> candidates;
    candidates.reserve(members.size());
    
    for (auto agentId : members) {
        candidates.emplace_back(agentId, agents[agentId].assertiveness);
    }
    
    // Sort by assertiveness descending
    std::sort(candidates.begin(), candidates.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    
    std::vector<std::uint32_t> leaders;
    leaders.reserve(topN);
    for (std::uint32_t i = 0; i < topN && i < candidates.size(); ++i) {
        leaders.push_back(candidates[i].first);
    }
    
    return leaders;
}

// Queries
Movement* MovementModule::findMovement(std::uint32_t id) {
    auto it = std::find_if(movements_.begin(), movements_.end(),
                           [id](const Movement& m) { return m.id == id; });
    return (it != movements_.end()) ? &(*it) : nullptr;
}

std::vector<Movement*> MovementModule::movementsInRegion(std::uint32_t regionId) {
    std::vector<Movement*> result;
    for (auto& mov : movements_) {
        if (mov.regionalStrength.count(regionId) > 0) {
            result.push_back(&mov);
        }
    }
    return result;
}

std::vector<Movement*> MovementModule::movementsByPower() const {
    std::vector<Movement*> result;
    result.reserve(movements_.size());
    for (auto& mov : movements_) {
        result.push_back(const_cast<Movement*>(&mov));
    }
    std::sort(result.begin(), result.end(),
              [](const Movement* a, const Movement* b) { return a->power > b->power; });
    return result;
}

// Statistics
MovementModule::MovementStats MovementModule::computeStats() const {
    MovementStats stats;
    stats.totalMovements = movements_.size();
    
    double totalPower = 0.0;
    double totalSize = 0.0;
    std::set<std::uint32_t> allMembers;
    
    for (const auto& mov : movements_) {
        switch (mov.stage) {
            case MovementStage::Birth: stats.birthStage++; break;
            case MovementStage::Growth: stats.growthStage++; break;
            case MovementStage::Plateau: stats.plateauStage++; break;
            case MovementStage::Decline: stats.declineStage++; break;
            default: break;
        }
        
        totalPower += mov.power;
        totalSize += mov.members.size();
        allMembers.insert(mov.members.begin(), mov.members.end());
    }
    
    if (stats.totalMovements > 0) {
        stats.avgPower = totalPower / stats.totalMovements;
        stats.avgSize = totalSize / stats.totalMovements;
    }
    stats.totalMembership = allMembers.size();
    
    return stats;
}
