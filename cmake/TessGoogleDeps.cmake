include_guard(DIRECTORY)

include(FetchContent)

set(TESS_GOOGLETEST_VERSION "v1.17.0")
set(TESS_GOOGLE_BENCHMARK_VERSION "v1.9.5")

function(tess_require_googletest)
  find_package(GTest CONFIG QUIET)
  if(GTest_FOUND)
    return()
  endif()

  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG "${TESS_GOOGLETEST_VERSION}"
    GIT_SHALLOW TRUE
    SYSTEM
    EXCLUDE_FROM_ALL
  )
  FetchContent_MakeAvailable(googletest)
endfunction()

function(tess_require_google_benchmark)
  find_package(benchmark CONFIG QUIET)
  if(benchmark_FOUND)
    return()
  endif()

  set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
  set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
  set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)

  FetchContent_Declare(
    googlebenchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG "${TESS_GOOGLE_BENCHMARK_VERSION}"
    GIT_SHALLOW TRUE
    SYSTEM
    EXCLUDE_FROM_ALL
  )
  FetchContent_MakeAvailable(googlebenchmark)
endfunction()
