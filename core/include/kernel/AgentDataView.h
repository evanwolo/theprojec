#pragma once
#include <cstdint>

// Macro to allow this struct to be used in both C++ and CUDA code
#ifdef __CUDACC__
#define HOST_DEVICE __host__ __device__
#else
#define HOST_DEVICE
#endif

struct AgentDataView {
    uint32_t count;

    // --- Beliefs (Structure of Arrays) ---
    // Splitting 4D beliefs into 4 arrays allows vector loading
    double* __restrict__ B0; // Authority-Liberty
    double* __restrict__ B1; // Tradition-Progress
    double* __restrict__ B2; // Hierarchy-Equality
    double* __restrict__ B3; // Isolation-Unity

    // --- Physics Properties ---
    double* __restrict__ susceptibility; // Modulated by Economy/Psych
    double* __restrict__ fluency;        // Language fluency
    std::uint8_t* __restrict__ primaryLang; // Dominant language per agent
    std::uint8_t* __restrict__ alive;       // Alive flag (1=alive)

    // --- Social Network (CSR Format) ---
    // We replace std::vector<std::vector<int>> with Compressed Sparse Row.
    // GPU friendly: Graph is one giant contiguous array.
    // neighbors[i] are found at: neighbor_indices[ offsets[i] ... offsets[i] + counts[i] ]
    int* __restrict__ neighbor_offsets; 
    int* __restrict__ neighbor_counts;
    int* __restrict__ neighbor_indices; 
    
    // --- Accessor Helper (Runs on CPU & GPU) ---
    HOST_DEVICE inline int getNeighbor(int agentIdx, int nbrIndex) const {
        int start = neighbor_offsets[agentIdx];
        return neighbor_indices[start + nbrIndex];
    }
};
