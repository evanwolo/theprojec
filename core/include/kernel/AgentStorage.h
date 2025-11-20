#pragma once
#include <vector>
#include <algorithm>
#include <cstdint>
#include "kernel/AgentDataView.h"
#include "kernel/Agent.h" 

class AgentStorage {
private:
    // Core Data (SoA)
    std::vector<float> B0, B1, B2, B3;
    std::vector<float> susceptibility;
    std::vector<float> fluency;
    std::vector<std::uint8_t> primaryLang;
    std::vector<std::uint8_t> alive;

    // Network Data (CSR)
    std::vector<int> neighbor_offsets;
    std::vector<int> neighbor_counts;
    std::vector<int> neighbor_indices;

    bool beliefsDirty_ = true;
    bool propsDirty_ = true;
    bool graphDirty_ = true;

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
    void syncFromLegacy(const std::vector<Agent>& legacyAgents, bool rebuildGraph) {
        size_t n = legacyAgents.size();
        bool sizeChanged = (B0.size() != n);
        if (sizeChanged) {
            resize(n);
            rebuildGraph = true;
        }

        bool beliefsChanged = sizeChanged;
        bool propsChanged = sizeChanged;

        if (rebuildGraph) {
            neighbor_indices.clear();
        }

        for(size_t i = 0; i < n; ++i) {
            const auto& a = legacyAgents[i];
            
            // Copy Beliefs
            float newB0 = static_cast<float>(a.B[0]);
            float newB1 = static_cast<float>(a.B[1]);
            float newB2 = static_cast<float>(a.B[2]);
            float newB3 = static_cast<float>(a.B[3]);
            if (sizeChanged || newB0 != B0[i] || newB1 != B1[i] ||
                newB2 != B2[i] || newB3 != B3[i]) {
                beliefsChanged = true;
                B0[i] = newB0;
                B1[i] = newB1;
                B2[i] = newB2;
                B3[i] = newB3;
            }
            
            // Copy Properties
            float newSus = static_cast<float>(a.m_susceptibility);
            float newFlu = static_cast<float>(a.fluency);
            std::uint8_t newLang = a.primaryLang;
            std::uint8_t newAlive = static_cast<std::uint8_t>(a.alive ? 1 : 0);

            if (sizeChanged || newSus != susceptibility[i] || newFlu != fluency[i] ||
                newLang != primaryLang[i] || newAlive != alive[i]) {
                propsChanged = true;
                susceptibility[i] = newSus;
                fluency[i] = newFlu;
                primaryLang[i] = newLang;
                alive[i] = newAlive;
            }

            // Flatten Network (Adjacency List -> CSR)
            if (rebuildGraph) {
                neighbor_offsets[i] = static_cast<int>(neighbor_indices.size());
                neighbor_counts[i] = static_cast<int>(a.neighbors.size());
                for(uint32_t nbr : a.neighbors) {
                    neighbor_indices.push_back(static_cast<int>(nbr));
                }
            }
        }

        if (beliefsChanged) beliefsDirty_ = true;
        if (propsChanged) propsDirty_ = true;
        if (rebuildGraph) {
            graphDirty_ = true;
        }
    }

    // Syncs data BACK to legacy agents (so Economy/IO modules still work)
    void syncToLegacy(std::vector<Agent>& legacyAgents) {
        size_t n = legacyAgents.size();
        for(size_t i = 0; i < n; ++i) {
            auto& a = legacyAgents[i];
            a.B[0] = static_cast<double>(B0[i]);
            a.B[1] = static_cast<double>(B1[i]);
            a.B[2] = static_cast<double>(B2[i]);
            a.B[3] = static_cast<double>(B3[i]);
            // Note: We typically don't sync network BACK unless it changed
        }
    }

    // Add this method
    size_t getEdgeCount() const {
        return neighbor_indices.size();
    }

    bool needsBeliefUpload() const { return beliefsDirty_; }
    bool needsPropertyUpload() const { return propsDirty_; }
    bool needsGraphUpload() const { return graphDirty_; }

    void markBeliefsClean() { beliefsDirty_ = false; }
    void markPropertiesClean() { propsDirty_ = false; }
    void markGraphClean() { graphDirty_ = false; }
};
