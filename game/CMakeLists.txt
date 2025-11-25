# Game module CMakeLists.txt
# Contains game-specific logic that uses the core simulation engine
cmake_minimum_required(VERSION 3.15)

# Collect all source files
file(GLOB_RECURSE GAME_SOURCES
  "src/**/*.cpp"
)

file(GLOB_RECURSE GAME_HEADERS
  "include/**/*.h"
)

# Create static library
add_library(civilizationgame STATIC ${GAME_SOURCES} ${GAME_HEADERS})

# Include directories
target_include_directories(civilizationgame
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Link to core engine
target_link_libraries(civilizationgame PUBLIC civilizationengine)

# Compiler features
target_compile_features(civilizationgame PUBLIC cxx_std_17)

# Set library properties
set_target_properties(civilizationgame PROPERTIES
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
)
