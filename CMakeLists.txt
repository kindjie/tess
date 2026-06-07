cmake_minimum_required(VERSION 3.28)

project(
  tess
  VERSION 0.1.0
  DESCRIPTION "Performance-first tile and path simulation substrate"
  LANGUAGES CXX
)

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

set(CMAKE_CXX_SCAN_FOR_MODULES 0)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(TESS_BUILD_TESTING "Build tess tests" ${PROJECT_IS_TOP_LEVEL})
option(TESS_BUILD_EXAMPLES "Build tess examples" ${PROJECT_IS_TOP_LEVEL})
option(TESS_BUILD_BENCHMARKS "Build tess benchmarks" OFF)
option(TESS_ENABLE_WARNINGS "Enable tess warning flags on local targets" ON)
option(TESS_WARNINGS_AS_ERRORS "Treat tess local target warnings as errors" OFF)
option(TESS_ENABLE_CLANG_TIDY "Enable clang-tidy on tess local targets" OFF)
set(TESS_CLANG_TIDY_CONFIG "" CACHE FILEPATH "Optional clang-tidy config file")
option(TESS_ENABLE_CPPCHECK "Enable cppcheck on tess local targets" OFF)
option(TESS_ENABLE_SANITIZERS "Enable ASan and UBSan on tess local targets" OFF)

include(TessProjectOptions)

if(TESS_ENABLE_CLANG_TIDY)
  find_program(TESS_CLANG_TIDY_EXE NAMES clang-tidy REQUIRED)
endif()

if(TESS_ENABLE_CPPCHECK)
  find_program(TESS_CPPCHECK_EXE NAMES cppcheck REQUIRED)
endif()

add_library(tess INTERFACE)
add_library(tess::tess ALIAS tess)

target_compile_features(tess INTERFACE cxx_std_20)

set(
  TESS_PUBLIC_HEADERS
  include/tess/block/block.h
  include/tess/core/shape.h
  include/tess/diagnostics/diagnostics.h
  include/tess/ops/queued.h
  include/tess/path/detail/weighted_batch.h
  include/tess/path/distance_field_box.h
  include/tess/path/field_product_cache.h
  include/tess/path/path.h
  include/tess/path/path_runtime.h
  include/tess/path/portal_route.h
  include/tess/path/portal_segment_cache.h
  include/tess/sim/movement.h
  include/tess/sim/path_agent.h
  include/tess/sim/path_agent_tick.h
  include/tess/sim/render_delta.h
  include/tess/sim/scheduler.h
  include/tess/sim/time.h
  include/tess/storage/chunk_page.h
  include/tess/storage/world.h
  include/tess/tess.h
  include/tess/topology/topology.h
)

target_sources(
  tess
  INTERFACE
    FILE_SET HEADERS
    BASE_DIRS include
    FILES ${TESS_PUBLIC_HEADERS}
)

install(
  TARGETS tess
  EXPORT tess-targets
  FILE_SET HEADERS
)

install(
  EXPORT tess-targets
  FILE tess-targets.cmake
  NAMESPACE tess::
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/tess"
)

configure_package_config_file(
  cmake/tess-config.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/tess-config.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/tess"
)

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/tess-config-version.cmake"
  VERSION "${PROJECT_VERSION}"
  COMPATIBILITY SameMajorVersion
)

install(
  FILES
    "${CMAKE_CURRENT_BINARY_DIR}/tess-config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/tess-config-version.cmake"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/tess"
)

if(TESS_BUILD_TESTING)
  include(CTest)
  if(BUILD_TESTING)
    add_test(
      NAME tess_public_headers_file_set
      COMMAND
        "${CMAKE_COMMAND}"
        -DTESS_SOURCE_DIR=${PROJECT_SOURCE_DIR}
        "-DTESS_DECLARED_HEADERS=$<JOIN:${TESS_PUBLIC_HEADERS},;>"
        -P ${PROJECT_SOURCE_DIR}/cmake/check-public-headers.cmake
    )
    add_subdirectory(tests)
  endif()
endif()

if(TESS_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

if(TESS_BUILD_BENCHMARKS)
  add_subdirectory(bench)
endif()
