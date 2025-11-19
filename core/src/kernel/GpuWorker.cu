#include "kernel/GpuWorker.h"
#include <cuda_runtime.h>
#include <iostream>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <cstdlib>

#define CUDA_CHECK(call) do { \
    cudaError_t err_ = (call); \
    if (err_ != cudaSuccess) { \
        checkCuda((#call), __FILE__, __LINE__, err_); \
    } \
} while (0)

// --- The CUDA Kernel (Runs on thousands of GPU threads) ---
__global__ void updateBeliefsKernel(AgentDataView view, double stepSize, double simFloor) {
    // 1. Identify which agent this thread is responsible for
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= view.count) return;
    if (!view.alive[i]) return;

    // 2. Load My State (Coalesced Memory Access)
    double myB0 = view.B0[i];
    double myB1 = view.B1[i];
    double myB2 = view.B2[i];
    double myB3 = view.B3[i];
    double mySusc = view.susceptibility[i];
    double myFluency = view.fluency[i];
    std::uint8_t myLang = view.primaryLang[i];

    double myNormSq = myB0*myB0 + myB1*myB1 + myB2*myB2 + myB3*myB3;

    // 3. Network Loop (Graph Convolution)
    int start = view.neighbor_offsets[i];
    int count = view.neighbor_counts[i];
    
    double d0 = 0.0, d1 = 0.0, d2 = 0.0, d3 = 0.0;

    for (int j = 0; j < count; ++j) {
        // Random Access into neighbor indices (Unavoidable for graph logic)
        int nbrId = view.neighbor_indices[start + j];

        // Load Neighbor State
        double tB0 = view.B0[nbrId];
        double tB1 = view.B1[nbrId];
        double tB2 = view.B2[nbrId];
        double tB3 = view.B3[nbrId];
        if (!view.alive[nbrId]) continue;
        double nbrFluency = view.fluency[nbrId];
        std::uint8_t nbrLang = view.primaryLang[nbrId];
        
        // --- Physics Logic (Identical to CPU version) ---
        double tNormSq = tB0*tB0 + tB1*tB1 + tB2*tB2 + tB3*tB3;
        double dot = myB0*tB0 + myB1*tB1 + myB2*tB2 + myB3*tB3;
        
        double sim = 1.0;
        double normProd = myNormSq * tNormSq;
        if (normProd > 1e-9) {
            sim = dot / sqrt(normProd);
        }
        
        // Similarity Gate
        double gate = (sim - simFloor) / (1.0 - simFloor);
        if (gate <= 0.0) continue; // Optimization: Skip calculation

        // Language Factor
        double langQ = (myLang == nbrLang) ? 0.5 * (myFluency + nbrFluency) : 0.1;
        if (langQ <= 0.0) continue;
        
        double weight = stepSize * gate * langQ * mySusc;

        // Apply Force using Fast Tanh Approximation
        auto fastTanh = [](double x) { 
            double x2 = x*x; 
            return x * (27.0 + x2) / (27.0 + 9.0 * x2); 
        };

        d0 += weight * fastTanh(tB0 - myB0);
        d1 += weight * fastTanh(tB1 - myB1);
        d2 += weight * fastTanh(tB2 - myB2);
        d3 += weight * fastTanh(tB3 - myB3);
    }

    // 4. Apply Deltas & Clamp (Write Back)
    // Note: For strict correctness, we should use a second buffer ("double buffering")
    // to avoid race conditions (reading while writing). However, for social physics,
    // asynchronous updates often add realistic noise. We stick to direct write for speed here.
    
    auto clamp = [](double v) { return (v < -1.0) ? -1.0 : (v > 1.0) ? 1.0 : v; };
    
    view.B0[i] = clamp(myB0 + d0);
    view.B1[i] = clamp(myB1 + d1);
    view.B2[i] = clamp(myB2 + d2);
    view.B3[i] = clamp(myB3 + d3);
}

// --- Host-Side Management ---

struct GpuContext {
    bool initialized = false;
    size_t agentCapacity = 0;
    size_t edgeCapacity = 0;
    
    // Device Pointers (VRAM)
    double *dB0 = nullptr, *dB1 = nullptr, *dB2 = nullptr, *dB3 = nullptr;
    double *dSus = nullptr, *dFlu = nullptr;
    std::uint8_t *dLang = nullptr, *dAlive = nullptr;
    int *dOff = nullptr, *dCnt = nullptr, *dIdx = nullptr;

    // Allocate VRAM
    void resize(size_t nAgents, size_t nEdges) {
        if (initialized) {
             cudaFree(dB0); cudaFree(dB1); cudaFree(dB2); cudaFree(dB3);
             cudaFree(dSus); cudaFree(dFlu);
             cudaFree(dLang); cudaFree(dAlive);
             cudaFree(dOff); cudaFree(dCnt); cudaFree(dIdx);
        }
        
        // Allocate Physics Arrays
        CUDA_CHECK(cudaMalloc(&dB0, nAgents * sizeof(double)));
        CUDA_CHECK(cudaMalloc(&dB1, nAgents * sizeof(double)));
        CUDA_CHECK(cudaMalloc(&dB2, nAgents * sizeof(double)));
        CUDA_CHECK(cudaMalloc(&dB3, nAgents * sizeof(double)));
        CUDA_CHECK(cudaMalloc(&dSus, nAgents * sizeof(double)));
        CUDA_CHECK(cudaMalloc(&dFlu, nAgents * sizeof(double)));
        CUDA_CHECK(cudaMalloc(&dLang, nAgents * sizeof(std::uint8_t)));
        CUDA_CHECK(cudaMalloc(&dAlive, nAgents * sizeof(std::uint8_t)));
        
        // Allocate Network Arrays
        CUDA_CHECK(cudaMalloc(&dOff, nAgents * sizeof(int)));
        CUDA_CHECK(cudaMalloc(&dCnt, nAgents * sizeof(int)));
        CUDA_CHECK(cudaMalloc(&dIdx, nEdges * sizeof(int)));
        
        agentCapacity = nAgents;
        edgeCapacity = nEdges;
        initialized = true;
    }
} g_ctx;

void launchGpuBeliefUpdate(AgentStorage& hostStorage, const KernelConfig& cfg) {
    AgentDataView hView = hostStorage.getView();
    size_t nAgents = hView.count;
    size_t nEdges = hostStorage.getEdgeCount();

    // 1. Allocate if needed
    if (!g_ctx.initialized || g_ctx.agentCapacity < nAgents || g_ctx.edgeCapacity < nEdges) {
        g_ctx.resize(nAgents, nEdges);
    }

    // 2. Copy Host -> Device (PCIe Transfer)
    // In a production engine, we would track "dirty" flags to only copy changed data.
    CUDA_CHECK(cudaMemcpy(g_ctx.dB0, hView.B0, nAgents * sizeof(double), cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(g_ctx.dB1, hView.B1, nAgents * sizeof(double), cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(g_ctx.dB2, hView.B2, nAgents * sizeof(double), cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(g_ctx.dB3, hView.B3, nAgents * sizeof(double), cudaMemcpyHostToDevice));
    
    CUDA_CHECK(cudaMemcpy(g_ctx.dSus, hView.susceptibility, nAgents * sizeof(double), cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(g_ctx.dFlu, hView.fluency, nAgents * sizeof(double), cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(g_ctx.dLang, hView.primaryLang, nAgents * sizeof(std::uint8_t), cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(g_ctx.dAlive, hView.alive, nAgents * sizeof(std::uint8_t), cudaMemcpyHostToDevice));

    CUDA_CHECK(cudaMemcpy(g_ctx.dOff, hView.neighbor_offsets, nAgents * sizeof(int), cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(g_ctx.dCnt, hView.neighbor_counts, nAgents * sizeof(int), cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(g_ctx.dIdx, hView.neighbor_indices, nEdges * sizeof(int), cudaMemcpyHostToDevice));

    // 3. Construct Device View
    AgentDataView dView = hView; // Copy counts
    dView.B0 = g_ctx.dB0; dView.B1 = g_ctx.dB1; dView.B2 = g_ctx.dB2; dView.B3 = g_ctx.dB3;
    dView.susceptibility = g_ctx.dSus; dView.fluency = g_ctx.dFlu;
    dView.primaryLang = g_ctx.dLang; dView.alive = g_ctx.dAlive;
    dView.neighbor_offsets = g_ctx.dOff; dView.neighbor_counts = g_ctx.dCnt;
    dView.neighbor_indices = g_ctx.dIdx;

    // 4. Launch
    int threads = 256;
    int blocks = (nAgents + threads - 1) / threads;
    
    updateBeliefsKernel<<<blocks, threads>>>(dView, cfg.stepSize, cfg.simFloor);
    
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());

    // 5. Copy Device -> Host (PCIe Transfer)
    CUDA_CHECK(cudaMemcpy(hView.B0, g_ctx.dB0, nAgents * sizeof(double), cudaMemcpyDeviceToHost));
    CUDA_CHECK(cudaMemcpy(hView.B1, g_ctx.dB1, nAgents * sizeof(double), cudaMemcpyDeviceToHost));
    CUDA_CHECK(cudaMemcpy(hView.B2, g_ctx.dB2, nAgents * sizeof(double), cudaMemcpyDeviceToHost));
    CUDA_CHECK(cudaMemcpy(hView.B3, g_ctx.dB3, nAgents * sizeof(double), cudaMemcpyDeviceToHost));
}

void checkCuda(const char* func, const char* file, int line, cudaError_t err) {
    std::cerr << "CUDA Error at " << file << ":" << line << " (" << func << "): " 
              << cudaGetErrorString(err) << std::endl;
    std::exit(EXIT_FAILURE);
}
