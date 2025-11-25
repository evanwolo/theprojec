# CLI CMakeLists.txt
cmake_minimum_required(VERSION 3.15)

# Main kernel CLI
add_executable(KernelSim main_kernel.cpp)

# Link against core engine and game libraries
target_link_libraries(KernelSim PRIVATE civilizationengine)
if(BUILD_GAME)
  target_link_libraries(KernelSim PRIVATE civilizationgame)
  target_compile_definitions(KernelSim PRIVATE HAS_GAME_MODULES)
endif()

# Include core and game headers
target_include_directories(KernelSim PRIVATE 
  ${CMAKE_SOURCE_DIR}/core/include
  ${CMAKE_SOURCE_DIR}/game/include
)

# Set output directory
set_target_properties(KernelSim PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)