cmake_minimum_required(VERSION 3.25)

project(tess_fetchcontent_consumer LANGUAGES CXX)

option(TESS_NO_EXCEPTIONS "Compile this consumer without exceptions" OFF)

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
if(TESS_NO_EXCEPTIONS)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    message(FATAL_ERROR "TESS_NO_EXCEPTIONS requires Clang-family or GCC")
  endif()
  target_compile_options(tess_fetchcontent_consumer PRIVATE -fno-exceptions)
  target_compile_definitions(
    tess_fetchcontent_consumer PRIVATE TESS_EXPECT_NO_EXCEPTIONS=1)
endif()
