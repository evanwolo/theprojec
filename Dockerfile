# Multi-stage build for Grand Strategy Simulation Engine (GPU Enabled)
FROM nvidia/cuda:12.3.1-devel-ubuntu22.04 AS builder

# Install CMake and dependencies
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y \
    cmake \
    build-essential \
    git \
    libgomp1 \
    && rm -rf /var/lib/apt/lists/*

# Set working directory
WORKDIR /app

# Copy source files
COPY CMakeLists.txt ./
COPY core/ ./core/
COPY cli/ ./cli/
COPY tests/ ./tests/

# Build the application
RUN mkdir build && cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release && \
    cmake --build . --target KernelSim --parallel $(nproc)

# Runtime stage - CUDA runtime
FROM nvidia/cuda:12.3.1-runtime-ubuntu22.04

# Install basic runtime dependencies (OpenMP)
RUN apt-get update && apt-get install -y \
    libgomp1 \
    && rm -rf /var/lib/apt/lists/*

# Create app directory and data directory
WORKDIR /app
RUN mkdir -p /app/data

# Copy built executable from builder
COPY --from=builder /app/build/KernelSim /app/

# Create non-root user and set permissions
RUN useradd -m -u 1000 simuser && \
    chown -R simuser:simuser /app

USER simuser

# Default command
CMD ["/app/KernelSim"]
