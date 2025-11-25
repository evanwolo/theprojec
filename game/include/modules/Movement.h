#ifndef GAME_MOVEMENT_H
#define GAME_MOVEMENT_H

#include <array>
#include <vector>
#include <map>
#include <string>
#include <cstdint>

// Forward declarations
class Kernel;
struct Cluster;

// Movement lifecycle stages
enum class MovementStage {
    Birth,
    Growth,
    Plateau,
    Schism,
    Decline,
    Dead
};

// Movement structure
struct Movement {
    std::uint32_t id = 0;
    std::uint64_t birthTick = 0;
    std::uint64_t lastUpdateTick = 0;
    MovementStage stage = MovementStage::Birth;
    
    // Platform (mean beliefs of members)
    std::array<double, 4> platform{0, 0, 0, 0};
    
    // Membership
    std::vector<std::uint32_t> members;
    std::vector<std::uint32_t> leaders;  // High-assertiveness agents
    
    // Geographic presence
    std::map<std::uint32_t, double> regionalStrength;  // region -> proportion
    
    // Power metrics
    double power = 0.0;              // Overall power (0-1)
    double streetCapacity = 0.0;     // Protest/strike capacity
    double mediaPresence = 0.0;      // Future: media influence
    double institutionalAccess = 0.0; // Future: captured institutions
    
    // Economic base
    std::map<int, double> classComposition;  // wealth decile -> proportion
    
    // Dynamics
    double coherence = 0.0;          // Internal belief alignment
    double momentum = 0.0;           // Growth rate (members gained/lost per tick)
    double charismaScore = 0.0;      // Average leader assertiveness
};

// Formation thresholds
struct MovementFormationConfig {
    std::uint32_t minSize = 50;          // Lowered from 100
    double minCoherence = 0.6;           // Lowered from 0.7
    double minCharismaDensity = 0.05;    // Lowered from 0.6
    double minMomentum = 0.0;  // Future: growth-based formation
    
    // Economic triggers
    double hardshipThreshold = 0.4;      // Lowered from 0.5
    double inequalityThreshold = 0.5;    // Lowered from 0.6
};

// Movement module
class MovementModule {
public:
    explicit MovementModule(const MovementFormationConfig& cfg = MovementFormationConfig());
    
    // Update movements based on kernel state
    void update(Kernel& kernel, const std::vector<Cluster>& clusters, std::uint64_t tick);
    
    // Access
    const std::vector<Movement>& movements() const { return movements_; }
    std::vector<Movement>& movementsMut() { return movements_; }
    
    // Queries
    Movement* findMovement(std::uint32_t id);
    std::vector<Movement*> movementsInRegion(std::uint32_t regionId);
    std::vector<Movement*> movementsByPower() const;  // Sorted descending
    
    // Statistics
    struct MovementStats {
        std::uint32_t totalMovements = 0;
        std::uint32_t birthStage = 0;
        std::uint32_t growthStage = 0;
        std::uint32_t plateauStage = 0;
        std::uint32_t declineStage = 0;
        double avgPower = 0.0;
        double avgSize = 0.0;
        double totalMembership = 0;  // Agents in any movement
    };
    MovementStats computeStats() const;
    
private:
    MovementFormationConfig cfg_;
    std::vector<Movement> movements_;
    std::uint32_t nextId_ = 0;
    
    // Formation logic
    void detectFormations(Kernel& kernel, const std::vector<Cluster>& clusters, std::uint64_t tick);
    bool shouldFormMovement(const Cluster& cluster, const Kernel& kernel) const;
    Movement createMovement(const Cluster& cluster, const Kernel& kernel, std::uint64_t tick);
    
    // Update logic
    void updateExistingMovements(Kernel& kernel, std::uint64_t tick);
    void updateMembership(Movement& mov, const Kernel& kernel);
    void updatePowerMetrics(Movement& mov, const Kernel& kernel);
    void updateStage(Movement& mov);
    void pruneDeadMovements();
    
    // Leaders
    std::vector<std::uint32_t> identifyLeaders(const std::vector<std::uint32_t>& members,
                                                const Kernel& kernel,
                                                std::uint32_t topN = 5) const;
};

#endif
