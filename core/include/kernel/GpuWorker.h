#pragma once
#include <cuda_runtime.h>
#include "kernel/AgentStorage.h"
#include "kernel/Kernel.h" // For KernelConfig

// Entry point: Transfers data to GPU, runs physics, syncs back.
void launchGpuBeliefUpdate(AgentStorage& hostStorage, const KernelConfig& cfg);

// Utility to check for CUDA errors
void checkCuda(const char* func, const char* file, int line, cudaError_t err);
