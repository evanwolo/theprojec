cmake_minimum_required(VERSION 3.15)
project(GrandStrategySim VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)  # For IDE integration

# Options
option(BUILD_TESTS "Build test suite" ON)
option(BUILD_LEGACY "Build legacy prototype" OFF)
option(ENABLE_OPENMP "Enable OpenMP parallelization" ON)

# Compiler flags
if(MSVC)
  add_compile_options(/W4 /O2 /fp:fast)
else()
  add_compile_options(-Wall -Wextra -pedantic)
  set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG -ffast-math -funroll-loops -finline-functions")
endif()

# OpenMP
if(ENABLE_OPENMP)
  find_package(OpenMP)
  if(OpenMP_CXX_FOUND)
    add_compile_definitions(USE_OPENMP)
  endif()
endif()

# Core library
add_subdirectory(core)

# CLI executables
add_subdirectory(cli)

# Tests
if(BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

# Legacy prototype
if(BUILD_LEGACY)
  add_subdirectory(legacy)
endif()
