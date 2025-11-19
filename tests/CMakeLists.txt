# Tests CMakeLists.txt
cmake_minimum_required(VERSION 3.15)
find_package(CUDAToolkit REQUIRED)

# Find test framework (using Google Test as example)
find_package(GTest QUIET)
if(NOT GTest_FOUND)
  # Download and build Google Test
  include(FetchContent)
  FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/release-1.12.1.zip
  )
  FetchContent_MakeAvailable(googletest)
endif()

# Enable testing
enable_testing()

# Kernel tests
add_executable(kernel_tests kernel_tests.cpp)
target_link_libraries(kernel_tests PRIVATE grandstrategy CUDA::cudart CUDA::cudadevrt GTest::gtest_main)
add_test(NAME KernelTests COMMAND kernel_tests)

# Economy tests
add_executable(economy_tests economy_tests.cpp)
target_link_libraries(economy_tests PRIVATE grandstrategy CUDA::cudart CUDA::cudadevrt GTest::gtest_main)
add_test(NAME EconomyTests COMMAND economy_tests)
