include_guard(DIRECTORY)

include(FetchContent)
include("${CMAKE_CURRENT_LIST_DIR}/TessCMakeCompatibility.cmake")

# Commit SHAs pinned so upstream tag moves cannot alter builds.
set(TESS_GOOGLETEST_VERSION
    "52eb8108c5bdec04579160ae17225d66034bd723") # tag v1.17.0
set(TESS_GOOGLETEST_MIN_VERSION "1.17.0")
set(TESS_GOOGLE_BENCHMARK_VERSION
    "192ef10025eb2c4cdd392bc502f0c852196baa48") # tag v1.9.5
set(TESS_GOOGLE_BENCHMARK_MIN_VERSION "1.9.5")

function(tess_require_googletest)
  if(TARGET GTest::gtest_main)
    message(
      STATUS
      "Using trusted pre-existing GTest::gtest_main; version validation is "
      "the parent project's responsibility"
    )
    return()
  endif()

  if(TESS_USE_SYSTEM_DEPENDENCIES)
    find_package(GTest ${TESS_GOOGLETEST_MIN_VERSION} CONFIG REQUIRED)
    if(NOT TARGET GTest::gtest_main)
      message(FATAL_ERROR "GTest did not provide GTest::gtest_main")
    endif()
    return()
  endif()

  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG "${TESS_GOOGLETEST_VERSION}"
    SYSTEM
    ${TESS_FETCHCONTENT_EXCLUDE_FROM_ALL}
  )
  FetchContent_MakeAvailable(googletest)
endfunction()

function(tess_require_google_benchmark)
  if(TARGET benchmark::benchmark_main)
    message(
      STATUS
      "Using trusted pre-existing benchmark::benchmark_main; version "
      "validation is the parent project's responsibility"
    )
    return()
  endif()

  if(TESS_USE_SYSTEM_DEPENDENCIES)
    find_package(
      benchmark ${TESS_GOOGLE_BENCHMARK_MIN_VERSION} CONFIG REQUIRED
    )
    if(NOT TARGET benchmark::benchmark_main)
      message(FATAL_ERROR
              "Google Benchmark did not provide benchmark::benchmark_main")
    endif()
    return()
  endif()

  set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
  set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
  set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)

  FetchContent_Declare(
    googlebenchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG "${TESS_GOOGLE_BENCHMARK_VERSION}"
    SYSTEM
    ${TESS_FETCHCONTENT_EXCLUDE_FROM_ALL}
  )
  FetchContent_MakeAvailable(googlebenchmark)
endfunction()
