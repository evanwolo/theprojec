#pragma once
#include <vector>
#include <algorithm>
#include <cstdint>
#include "kernel/AgentDataView.h"
#include "kernel/Agent.h" 

class AgentStorage {
private:
    // Core Data (SoA)
    std::vector<double> B0, B1, B2, B3;
    std::vector<double> susceptibility;
    std::vector<double> fluency;
    std::vector<std::uint8_t> primaryLang;
    std::vector<std::uint8_t> alive;

    // Network Data (CSR)
    std::vector<int> neighbor_offsets;
    std::vector<int> neighbor_counts;
    std::vector<int> neighbor_indices;

public:
    // Resize all arrays to match population
    void resize(size_t size) {
        B0.resize(size);
        B1.resize(size);
        B2.resize(size);
        B3.resize(size);
        susceptibility.resize(size);
        fluency.resize(size);
        primaryLang.resize(size);
        alive.resize(size);
        
        neighbor_offsets.resize(size);
        neighbor_counts.resize(size);
        // neighbor_indices is dynamic, resized during network build
    }

    // Generate the View to pass to Kernels
    AgentDataView getView() {
        return AgentDataView{
            static_cast<uint32_t>(B0.size()),
            B0.data(), B1.data(), B2.data(), B3.data(),
            susceptibility.data(), fluency.data(), primaryLang.data(), alive.data(),
            neighbor_offsets.data(), neighbor_counts.data(), neighbor_indices.data()
        };
    }

    // --- Transition Helper ---
    // Syncs data FROM the legacy AoS Agent struct INTO this SoA storage.
    // Call this after 'initAgents' or 'buildSmallWorld'.
    void syncFromLegacy(const std::vector<Agent>& legacyAgents) {
        size_t n = legacyAgents.size();
        resize(n);
        neighbor_indices.clear();

        for(size_t i = 0; i < n; ++i) {
            const auto& a = legacyAgents[i];
            
            // Copy Beliefs
            B0[i] = a.B[0];
            B1[i] = a.B[1];
            B2[i] = a.B[2];
            B3[i] = a.B[3];
            
            // Copy Properties
            susceptibility[i] = a.m_susceptibility;
            fluency[i] = a.fluency;
            primaryLang[i] = a.primaryLang;
            alive[i] = static_cast<std::uint8_t>(a.alive ? 1 : 0);

            // Flatten Network (Adjacency List -> CSR)
            neighbor_offsets[i] = static_cast<int>(neighbor_indices.size());
            neighbor_counts[i] = static_cast<int>(a.neighbors.size());
            
            for(uint32_t nbr : a.neighbors) {
                neighbor_indices.push_back(static_cast<int>(nbr));
            }
        }
    }

    // Syncs data BACK to legacy agents (so Economy/IO modules still work)
    void syncToLegacy(std::vector<Agent>& legacyAgents) {
        size_t n = legacyAgents.size();
        for(size_t i = 0; i < n; ++i) {
            auto& a = legacyAgents[i];
            a.B[0] = B0[i];
            a.B[1] = B1[i];
            a.B[2] = B2[i];
            a.B[3] = B3[i];
            // Note: We typically don't sync network BACK unless it changed
        }
    }

    // Add this method
    size_t getEdgeCount() const {
        return neighbor_indices.size();
    }
};
