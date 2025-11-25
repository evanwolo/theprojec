# Core Civilization Engine library CMakeLists.txt
# This is the core simulation engine - game-agnostic agent-based modeling
cmake_minimum_required(VERSION 3.15)

# Collect all source files (excluding Movement which is game-specific)
set(CORE_SOURCES
  src/kernel/Kernel.cpp
  src/io/Snapshot.cpp
  src/modules/Culture.cpp
  src/modules/Economy.cpp
  src/modules/Health.cpp
  src/modules/MeanField.cpp
  src/modules/OnlineClustering.cpp
  src/modules/Psychology.cpp
  src/modules/TradeNetwork.cpp
  src/modules/CohortDemographics.cpp
  src/utils/EventLog.cpp
)

file(GLOB_RECURSE CORE_HEADERS
  "include/**/*.h"
)

# Exclude Movement.h from core headers (it's in game/)
list(FILTER CORE_HEADERS EXCLUDE REGEX ".*Movement\\.h$")

# Create static library
add_library(civilizationengine STATIC ${CORE_SOURCES} ${CORE_HEADERS})

# Include directories
target_include_directories(civilizationengine
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Link dependencies
find_package(OpenMP)
if(OpenMP_CXX_FOUND)
  target_link_libraries(civilizationengine PUBLIC OpenMP::OpenMP_CXX)
endif()

# Compiler features
target_compile_features(civilizationengine PUBLIC cxx_std_17)

# Set library properties
set_target_properties(civilizationengine PROPERTIES
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
)