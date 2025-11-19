#!/bin/bash
# Integration test for Grand Strategy Kernel

# Ensure we are in the correct directory
cd "$(dirname "$0")/.."

# Build the Docker image if it doesn't exist
if [[ "$(docker images -q theprojec:latest 2> /dev/null)" == "" ]]; then
  echo "Building Docker image..."
  docker build -t theprojec:latest .
fi

# Run a long simulation and check for movement formation
echo "Running 5000-tick simulation to test movement formation..."
docker run --rm -i theprojec:latest <<EOF | tee test-output.txt
run 5000 100
movements
quit
EOF

# Check if any movements were formed
if grep -q "Total Movements: 0" test-output.txt; then
  echo "TEST FAILED: No movements were formed after 5000 ticks."
  exit 1
else
  echo "TEST PASSED: Movements were successfully formed."
fi

# Clean up
rm test-output.txt
