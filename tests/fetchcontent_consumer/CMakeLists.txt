cmake_minimum_required(VERSION 3.25)

project(tess_fetchcontent_consumer LANGUAGES CXX)

if(NOT DEFINED TESS_SOURCE_DIR)
  message(FATAL_ERROR "TESS_SOURCE_DIR is required")
endif()

include(FetchContent)
FetchContent_Declare(tess SOURCE_DIR "${TESS_SOURCE_DIR}")
FetchContent_MakeAvailable(tess)

if(TESS_BUILD_TESTING OR TESS_BUILD_EXAMPLES OR TESS_BUILD_BENCHMARKS)
  message(FATAL_ERROR "tess enabled developer facilities as a subproject")
endif()
if(NOT TARGET tess::tess)
  message(FATAL_ERROR "tess::tess target is missing")
endif()

add_executable(tess_fetchcontent_consumer main.cc)
target_link_libraries(tess_fetchcontent_consumer PRIVATE tess::tess)
