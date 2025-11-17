# CLI CMakeLists.txt
cmake_minimum_required(VERSION 3.15)

# Main kernel CLI
add_executable(KernelSim main_kernel.cpp)

# Link against core library
target_link_libraries(KernelSim PRIVATE grandstrategy)

# Include core headers
target_include_directories(KernelSim PRIVATE ${CMAKE_SOURCE_DIR}/core/include)

# Set output directory
set_target_properties(KernelSim PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

# Optional: Batch runner
# add_executable(batch_runner batch_runner.cpp)
# target_link_libraries(batch_runner PRIVATE grandstrategy)
# target_include_directories(batch_runner PRIVATE ${CMAKE_SOURCE_DIR}/core/include)