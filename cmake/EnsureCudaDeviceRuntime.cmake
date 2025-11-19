# Ensures CUDA::cudadevrt exists even on toolkits that omit it
if(NOT TARGET CUDA::cudadevrt)
  find_library(CUDA_CUDADEVRT_LIBRARY
               NAMES cudadevrt
               PATHS ${CUDAToolkit_LIBRARY_DIR} ${CUDAToolkit_LIBRARY_ROOT}
                     ${CMAKE_CUDA_IMPLICIT_LINK_DIRECTORIES}
               NO_DEFAULT_PATH)
  if(NOT CUDA_CUDADEVRT_LIBRARY)
    find_library(CUDA_CUDADEVRT_LIBRARY NAMES cudadevrt)
  endif()
  if(NOT CUDA_CUDADEVRT_LIBRARY)
    message(FATAL_ERROR "Unable to locate CUDA device runtime library (cudadevrt)")
  endif()
  add_library(CUDA::cudadevrt STATIC IMPORTED)
  set_target_properties(CUDA::cudadevrt PROPERTIES
    IMPORTED_LOCATION "${CUDA_CUDADEVRT_LIBRARY}")
endif()
