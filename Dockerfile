# Multi-stage build for Civilization Simulation Engine
FROM gcc:13 AS builder

# Install CMake and OpenMP
RUN apt-get update && apt-get install -y \
    cmake \
    make \
    libgomp1 \
    && rm -rf /var/lib/apt/lists/*

# Set working directory
WORKDIR /app

# Copy source files
COPY CMakeLists.txt ./
COPY core/ ./core/
COPY game/ ./game/
COPY cli/ ./cli/
COPY tests/ ./tests/

# Build the application
RUN mkdir build && cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release && \
    cmake --build . --target KernelSim && \
    strip KernelSim

# Runtime stage - debian with full gcc runtime
FROM debian:bookworm-slim

# Install basic runtime dependencies (OpenMP)
RUN apt-get update && apt-get install -y \
    libgomp1 \
    && rm -rf /var/lib/apt/lists/*

# Create app directory and data directory
WORKDIR /app
RUN mkdir -p /app/data

# Copy built executable from builder
COPY --from=builder /app/build/KernelSim /app/

# Copy newer libstdc++ from gcc:13 builder to ensure compatibility
COPY --from=builder /usr/local/lib64/libstdc++.so.6 /usr/lib/x86_64-linux-gnu/

# Create non-root user and set permissions
RUN useradd -m -u 1000 simuser && \
    chown -R simuser:simuser /app

USER simuser

# Default command (can be overridden)
CMD ["/app/KernelSim"]
