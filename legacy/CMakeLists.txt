# Legacy prototype CMakeLists.txt
cmake_minimum_required(VERSION 3.15)

# Legacy prototype (original 200-agent implementation)
add_executable(OpinionSim
  src/BeliefTypes.cpp
  src/Individual.cpp
  src/Network.cpp
  src/Simulation.cpp
  src/Snapshot.cpp
  src/main.cpp
)

# Include legacy headers
target_include_directories(OpinionSim PRIVATE include)

# Set output directory
set_target_properties(OpinionSim PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)