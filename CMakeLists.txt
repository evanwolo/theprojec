cmake_minimum_required(VERSION 3.15)
project(CivilizationSim VERSION 0.4.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)  # For IDE integration

# Options
option(BUILD_TESTS "Build test suite" ON)
option(BUILD_GAME "Build game-specific modules" ON)
option(ENABLE_OPENMP "Enable OpenMP parallelization" ON)

# Compiler flags
if(MSVC)
  add_compile_options(/W4 /O2 /arch:AVX2)
  # Enable vectorization reports in MSVC
  add_compile_options(/Qvec-report:2)
else()
  add_compile_options(-Wall -Wextra -pedantic)
  set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG -ftree-vectorize -ffast-math")
  # Enable vectorization reports in GCC/Clang
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    add_compile_options(-fopt-info-vec-optimized)
  endif()
endif()

# OpenMP
if(ENABLE_OPENMP)
  find_package(OpenMP)
  if(OpenMP_CXX_FOUND)
    add_compile_definitions(USE_OPENMP)
  endif()
endif()

# Core engine library (game-agnostic simulation)
add_subdirectory(core)

# Game-specific modules (movements, etc.)
if(BUILD_GAME)
  add_subdirectory(game)
endif()

# CLI executables
add_subdirectory(cli)

# Tests
if(BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
