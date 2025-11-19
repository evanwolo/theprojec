#pragma once

#include <cstdint>
#include <array>
#include <vector>
#include "modules/Psychology.h"
#include "modules/Health.h"

// ---------- Agent Structure ----------
struct Agent {
    // Identity
    std::uint32_t id = 0;
    std::uint32_t region = 0;
    bool alive = true;
    
    // Demography
    int age = 0;                        // in years (tick-based)
    bool female = false;
    
    // Lineage (Phase 2 integration point)
    std::int32_t parent_a = -1;
    std::int32_t parent_b = -1;
    std::uint32_t lineage_id = 0;
    
    // Language (primary for kernel; extend to repertoire in Phase 2)
    std::uint8_t primaryLang = 0;
    double fluency = 1.0;  // 0..1
    
    // Personality traits (0..1, mean ~0.5)
    double openness = 0.5;
    double conformity = 0.5;
    double assertiveness = 0.5;
    double sociality = 0.5;
    
    // Belief state: internal x (unbounded), observable B = tanh(x)
    std::array<double, 4> x{0, 0, 0, 0};  // internal state
    std::array<double, 4> B{0, 0, 0, 0};  // beliefs [-1,1]
    double B_norm_sq = 0.0; // cached squared norm of B
    
    // Module multipliers (written by tech/media/economy modules)
    double m_comm = 1.0;        // communication reach/speed
    double m_susceptibility = 1.0;  // influence susceptibility
    double m_mobility = 1.0;    // migration/relocation ease
    
    PsychologicalState psych;
    HealthState health;

    // Network (sparse adjacency)
    std::vector<std::uint32_t> neighbors;
};
