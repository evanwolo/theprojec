cmake_minimum_required(VERSION 3.15)
project(OpinionSim VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
  add_compile_options(/W4 /O2)
else()
  add_compile_options(-Wall -Wextra -pedantic)
  set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG")
endif()

include_directories(${PROJECT_SOURCE_DIR}/include)

# High-performance kernel executable (production)
add_executable(KernelSim
  src/Kernel.cpp
  src/KernelSnapshot.cpp
  src/Clustering.cpp
  src/main_kernel.cpp
)

# Legacy prototype (original implementation)
add_executable(OpinionSim
  src/BeliefTypes.cpp
  src/Individual.cpp
  src/Network.cpp
  src/Simulation.cpp
  src/Snapshot.cpp
  src/main.cpp
)
