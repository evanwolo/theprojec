# Core library CMakeLists.txt
cmake_minimum_required(VERSION 3.15)

# Collect all source files
file(GLOB_RECURSE CORE_SOURCES
  "src/**/*.cpp"
)

file(GLOB_RECURSE CORE_HEADERS
  "include/**/*.h"
)

# Create static library
add_library(grandstrategy STATIC ${CORE_SOURCES} ${CORE_HEADERS})

# Include directories
target_include_directories(grandstrategy
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Link dependencies
find_package(OpenMP)
if(OpenMP_CXX_FOUND)
  target_link_libraries(grandstrategy PUBLIC OpenMP::OpenMP_CXX)
endif()

# Compiler features
target_compile_features(grandstrategy PUBLIC cxx_std_17)

# Set library properties
set_target_properties(grandstrategy PROPERTIES
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
)