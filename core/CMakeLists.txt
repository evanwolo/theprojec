# Core library CMakeLists.txt
cmake_minimum_required(VERSION 3.15)

# 1. Enable CUDA
enable_language(CUDA)
set(CMAKE_CUDA_STANDARD 17)

# 2. Include .cu files
file(GLOB_RECURSE CORE_SOURCES
  "src/**/*.cpp"
  "src/**/*.cu"
)

file(GLOB_RECURSE CORE_HEADERS "include/**/*.h")

add_library(grandstrategy STATIC ${CORE_SOURCES} ${CORE_HEADERS})

target_include_directories(grandstrategy
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

find_package(OpenMP)
if(OpenMP_CXX_FOUND)
  target_link_libraries(grandstrategy PUBLIC OpenMP::OpenMP_CXX)
endif()

# 3. Link CUDA
target_link_libraries(grandstrategy PRIVATE CUDA::cudart)
target_compile_features(grandstrategy PUBLIC cxx_std_17)
target_compile_definitions(grandstrategy PRIVATE USE_CUDA)
set_property(TARGET grandstrategy PROPERTY CUDA_SEPARABLE_COMPILATION ON)