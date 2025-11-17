#!/bin/bash
# Build script for Linux/macOS

echo "Building Grand Strategy Simulation Engine..."
echo

mkdir -p build
cd build

if ! command -v cmake &> /dev/null; then
    echo "ERROR: CMake not found in PATH. Install CMake to build the project."
    exit 1
fi

echo "Configuring with CMake..."
cmake .. -DCMAKE_BUILD_TYPE=Release
if [ $? -ne 0 ]; then
    echo "ERROR: CMake configuration failed."
    exit 1
fi

echo "Building targets..."
cmake --build .
if [ $? -ne 0 ]; then
    echo "ERROR: Build failed."
    exit 1
fi

echo
echo "========================================"
echo "Build successful!"
echo "========================================"
echo
echo "Run the kernel simulation:"
echo "  ./build/KernelSim"
echo
