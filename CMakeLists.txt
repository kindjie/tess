cmake_minimum_required(VERSION 3.25...3.28)

include(cmake/tess-version.cmake)
include(cmake/TessCMakeCompatibility.cmake)

option(
  TESS_ENABLE_GRID_BENCHMARK_DATA
  "Build opt-in tests backed by external grid benchmark data"
  OFF
)
option(
  TESS_REQUIRE_GRID_BENCHMARK_DATA
  "Fail instead of skip when opt-in grid benchmark data is unavailable"
  OFF
)

if(TESS_REQUIRE_GRID_BENCHMARK_DATA AND
   NOT TESS_ENABLE_GRID_BENCHMARK_DATA)
  message(
    FATAL_ERROR
      "TESS_REQUIRE_GRID_BENCHMARK_DATA requires "
      "TESS_ENABLE_GRID_BENCHMARK_DATA=ON"
  )
endif()

project(
  tess
  VERSION "${TESS_VERSION}"
  DESCRIPTION "Performance-first tile and path simulation substrate"
  LANGUAGES CXX
)

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

if(TESS_CMAKE_HAS_MODULE_SCAN)
  set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
endif()
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(TESS_BUILD_TESTING "Build tess tests" ${PROJECT_IS_TOP_LEVEL})
option(TESS_BUILD_EXAMPLES "Build tess examples" ${PROJECT_IS_TOP_LEVEL})
option(TESS_BUILD_BENCHMARKS "Build tess benchmarks" OFF)
option(TESS_BUILD_DOCS "Build the tess API reference with Doxygen" OFF)
option(
  TESS_EXAMPLES_NO_EXCEPTIONS
  "Compile examples with language exceptions disabled"
  OFF
)
option(
  TESS_ENABLE_COVERAGE
  "Instrument tess consumers with Clang source-based coverage"
  OFF
)
set(
  TESS_GRID_BENCHMARK_DATA_DIR
  ""
  CACHE PATH
  "External grid benchmark cache; empty selects the platform cache default"
)
option(TESS_ENABLE_ENTT
       "Build EnTT-gated tess tests, examples, and benchmarks (requires EnTT)"
       OFF)
option(TESS_ENABLE_FLECS
       "Build Flecs-gated tests, examples, and benchmarks (requires Flecs)"
       OFF)
option(TESS_ENABLE_WARNINGS "Enable tess warning flags on local targets" ON)
option(TESS_WARNINGS_AS_ERRORS "Treat tess local target warnings as errors" OFF)
option(TESS_ENABLE_CLANG_TIDY "Enable clang-tidy on tess local targets" OFF)
set(TESS_CLANG_TIDY_CONFIG "" CACHE FILEPATH "Optional clang-tidy config file")
option(TESS_ENABLE_CPPCHECK "Enable cppcheck on tess local targets" OFF)
option(TESS_ENABLE_SANITIZERS "Enable sanitizers on tess local targets" OFF)
option(
  TESS_USE_SYSTEM_DEPENDENCIES
  "Use compatible system packages instead of pinned fetched dependencies"
  OFF
)
set(
  TESS_SANITIZERS
  "address,undefined"
  CACHE STRING
  "Comma-separated -fsanitize= list used when TESS_ENABLE_SANITIZERS=ON"
)

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

# Source-based coverage for the weekly advisory reports (redesign
# section 3.6). tess is header-only, so instrumentation necessarily
# lives in consuming translation units; routing the flags through the
# interface target instruments exactly those TUs and leaves fetched
# dependencies alone. BUILD_INTERFACE keeps the flags out of the
# installed export set. Clang-only: llvm-cov reads this format.
if(TESS_ENABLE_COVERAGE)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(
      FATAL_ERROR
      "TESS_ENABLE_COVERAGE requires Clang (source-based coverage); "
      "the compiler is ${CMAKE_CXX_COMPILER_ID}"
    )
  endif()
  target_compile_options(
    tess
    INTERFACE
      $<BUILD_INTERFACE:-fprofile-instr-generate -fcoverage-mapping>
  )
  target_link_options(
    tess
    INTERFACE
      $<BUILD_INTERFACE:-fprofile-instr-generate>
  )
endif()

set(TESS_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/include")
configure_file(
  include/tess/version.h.in
  "${TESS_GENERATED_INCLUDE_DIR}/tess/version.h"
  @ONLY
)

set(
  TESS_PUBLIC_HEADERS
  include/tess/block/block.h
  include/tess/block/pipeline.h
  include/tess/core/assert.h
  include/tess/core/capacity.h
  include/tess/core/config.h
  include/tess/core/lattice.h
  include/tess/core/shape.h
  include/tess/debug/imgui/panels.h
  include/tess/debug/imgui/tools.h
  include/tess/diagnostics/diagnostics.h
  include/tess/diagnostics/export.h
  include/tess/diagnostics/trace.h
  include/tess/diagnostics/warning_sink.h
  include/tess/experimental/maintenance.h
  include/tess/ecs/adapter.h
  include/tess/ecs/entity_handle.h
  include/tess/ecs/entt/entt_adapter.h
  include/tess/ecs/flecs/flecs_adapter.h
  include/tess/gpu/backend.h
  include/tess/gpu/descriptors.h
  include/tess/gpu/webgpu_backend.h
  include/tess/ops/async_work.h
  include/tess/ops/phase_executor.h
  include/tess/ops/queued.h
  include/tess/ops/result_channel.h
  include/tess/path/distance_field_box.h
  include/tess/path/field_product_cache.h
  include/tess/path/node_index_space.h
  include/tess/path/path.h
  include/tess/path/path_runtime.h
  include/tess/path/path_view.h
  include/tess/path/portal_route.h
  include/tess/path/portal_segment_cache.h
  include/tess/path/precheck.h
  include/tess/path/route_cache.h
  include/tess/pathfinding.h
  include/tess/persistence/archive.h
  include/tess/query/span.h
  include/tess/sim/auto_exec.h
  include/tess/sim/async_work_task.h
  include/tess/sim/delta_frame.h
  include/tess/sim/event_stream.h
  include/tess/sim/joint_movement.h
  include/tess/sim/movement.h
  include/tess/sim/path_agent.h
  include/tess/sim/path_agent_tick.h
  include/tess/sim/pibt_movement.h
  include/tess/sim/render_delta.h
  include/tess/sim/schedule.h
  include/tess/sim/scheduler.h
  include/tess/sim/time.h
  include/tess/spatial/area.h
  include/tess/spatial/local_coordination.h
  include/tess/spatial/tactical_assignment.h
  include/tess/storage/chunk_meta.h
  include/tess/storage/chunk_page.h
  include/tess/storage/residency.h
  include/tess/storage/sparse_world.h
  include/tess/storage/world.h
  include/tess/simulation.h
  include/tess/tess.h
  include/tess/topology/movement_class.h
  include/tess/topology/step_policy.h
  include/tess/topology/topology.h
  include/tess/topology/transition_model.h
  include/tess/topology/transition_provider.h
)

# Installed implementation headers are required by the template definitions in
# the public headers, but declarations in tess::detail are not supported API.
# Keep this list separate so surface/documentation checks do not accidentally
# turn implementation details into compatibility promises.
set(
  TESS_IMPLEMENTATION_HEADERS
  include/tess/core/tag_identity.h
  include/tess/core/fail_fast.h
  include/tess/core/uint128.h
  include/tess/path/detail/astar.h
  include/tess/path/detail/weighted_batch.h
)

set(TESS_INSTALL_HEADERS ${TESS_PUBLIC_HEADERS} ${TESS_IMPLEMENTATION_HEADERS})

# Public and implementation headers live in separate file sets so the
# standalone-compilation check can verify exactly the public contract.
# Implementation fragments are deliberately not standalone: the
# path/detail headers #error when included outside their owning public
# header. Both sets install to the same paths as before -- every
# interface file set of an exported target must be named in the install
# rule, so omitting implementation_headers there would drop the
# template definitions the public headers need.
target_sources(
  tess
  INTERFACE
    FILE_SET HEADERS
    BASE_DIRS include
    FILES ${TESS_PUBLIC_HEADERS}
  INTERFACE
    FILE_SET implementation_headers
    TYPE HEADERS
    BASE_DIRS include
    FILES ${TESS_IMPLEMENTATION_HEADERS}
  INTERFACE
    FILE_SET generated_headers
    TYPE HEADERS
    BASE_DIRS "${TESS_GENERATED_INCLUDE_DIR}"
    FILES "${TESS_GENERATED_INCLUDE_DIR}/tess/version.h"
)

# Standalone compilation of every public header: CMake generates one
# translation unit per header in the selected sets, each including only
# that header. The selection is explicit because an empty selector
# verifies every interface set, including the non-standalone
# implementation fragments.
set_target_properties(
  tess
  PROPERTIES
    VERIFY_INTERFACE_HEADER_SETS ON
    INTERFACE_HEADER_SETS_TO_VERIFY "HEADERS;generated_headers"
)

# A build-only mirror of the public header contract under the real compiler
# exception-disable flag. The installed tess target stays policy-neutral.
if(TESS_BUILD_TESTING AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  add_library(tess_no_exceptions_headers INTERFACE)
  target_link_libraries(tess_no_exceptions_headers INTERFACE tess::tess)
  target_compile_options(
    tess_no_exceptions_headers INTERFACE -fno-exceptions)
  target_sources(
    tess_no_exceptions_headers
    INTERFACE
      FILE_SET no_exceptions_headers
      TYPE HEADERS
      BASE_DIRS include
      FILES ${TESS_PUBLIC_HEADERS}
    INTERFACE
      FILE_SET no_exceptions_generated_headers
      TYPE HEADERS
      BASE_DIRS "${TESS_GENERATED_INCLUDE_DIR}"
      FILES "${TESS_GENERATED_INCLUDE_DIR}/tess/version.h"
  )
  set_target_properties(
    tess_no_exceptions_headers
    PROPERTIES
      VERIFY_INTERFACE_HEADER_SETS ON
      INTERFACE_HEADER_SETS_TO_VERIFY
        "no_exceptions_headers;no_exceptions_generated_headers"
  )
endif()

# Generated include sweeps for the consumer-contract probes: one
# header per line in declaration order and in reverse, regenerated from
# the declared public set so a new header joins both automatically.
set(TESS_HEADER_SWEEP_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/sweep")
file(MAKE_DIRECTORY "${TESS_HEADER_SWEEP_DIR}")
set(_tess_sweep_forward "#pragma once\n")
set(_tess_sweep_reverse_list ${TESS_PUBLIC_HEADERS})
list(REVERSE _tess_sweep_reverse_list)
set(_tess_sweep_reverse "#pragma once\n")
foreach(_header IN LISTS TESS_PUBLIC_HEADERS)
  string(REPLACE "include/" "" _relative "${_header}")
  string(APPEND _tess_sweep_forward "#include <${_relative}>\n")
endforeach()
foreach(_header IN LISTS _tess_sweep_reverse_list)
  string(REPLACE "include/" "" _relative "${_header}")
  string(APPEND _tess_sweep_reverse "#include <${_relative}>\n")
endforeach()
file(
  GENERATE
  OUTPUT "${TESS_HEADER_SWEEP_DIR}/tess_all_headers_forward.h"
  CONTENT "${_tess_sweep_forward}"
)
file(
  GENERATE
  OUTPUT "${TESS_HEADER_SWEEP_DIR}/tess_all_headers_reverse.h"
  CONTENT "${_tess_sweep_reverse}"
)

install(
  TARGETS tess
  EXPORT tess-targets
  FILE_SET HEADERS
  FILE_SET implementation_headers
  FILE_SET generated_headers
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
  COMPATIBILITY SameMinorVersion
  ARCH_INDEPENDENT
)

install(
  FILES
    "${CMAKE_CURRENT_BINARY_DIR}/tess-config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/tess-config-version.cmake"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/tess"
)

install(
  FILES LICENSE
  DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/tess"
)

if(TESS_BUILD_TESTING)
  include(CTest)
  if(BUILD_TESTING)
    add_test(
      NAME tess_installed_headers_file_set
      COMMAND
        "${CMAKE_COMMAND}"
        -DTESS_SOURCE_DIR=${PROJECT_SOURCE_DIR}
        "-DTESS_DECLARED_HEADERS=$<JOIN:${TESS_INSTALL_HEADERS},;>"
        -P ${PROJECT_SOURCE_DIR}/cmake/check-public-headers.cmake
    )
    # Seconds-cheap install-contract check; runs on every selective
    # pre-push (the -L selection would otherwise exclude it).
    set_tests_properties(
      tess_installed_headers_file_set
      PROPERTIES LABELS "prepush:always"
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

if(TESS_BUILD_DOCS)
  find_package(Doxygen REQUIRED)
  # Keep implementation namespaces out of the published reference. The
  # repository's own gate enforces namespace-scope public API comments;
  # Doxygen can still list public members without requiring one comment each.
  set(DOXYGEN_EXCLUDE_PATTERNS "*/detail/*")
  set(DOXYGEN_EXCLUDE_SYMBOLS
      "tess::detail"
      "tess::detail::*"
      "tess::*::detail"
      "tess::*::detail::*")
  set(DOXYGEN_GENERATE_HTML YES)
  # doxygen-awesome-css (vendored, docs/doxygen-awesome/README.md) requires
  # the stock LIGHT base style; brand colors come from tess-theme.css.
  set(DOXYGEN_HTML_COLORSTYLE LIGHT)
  set(DOXYGEN_HTML_EXTRA_STYLESHEET
      "${CMAKE_CURRENT_SOURCE_DIR}/docs/doxygen-awesome/doxygen-awesome.css"
      "${CMAKE_CURRENT_SOURCE_DIR}/docs/doxygen-awesome/doxygen-awesome-sidebar-only.css"
      "${CMAKE_CURRENT_SOURCE_DIR}/docs/doxygen-awesome/tess-theme.css")
  # Ship the vendored stylesheets' MIT permission notice with the copies.
  set(DOXYGEN_HTML_EXTRA_FILES
      "${CMAKE_CURRENT_SOURCE_DIR}/docs/doxygen-awesome/LICENSE")
  set(DOXYGEN_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/docs")
  set(DOXYGEN_PROJECT_LOGO
      "${CMAKE_CURRENT_SOURCE_DIR}/docs/assets/tess-symbol.svg")
  set(DOXYGEN_STRIP_FROM_INC_PATH
      "${CMAKE_CURRENT_SOURCE_DIR}/include"
      "${TESS_GENERATED_INCLUDE_DIR}")
  set(DOXYGEN_USE_MDFILE_AS_MAINPAGE
      "${CMAKE_CURRENT_SOURCE_DIR}/docs/api-main.md")
  set(DOXYGEN_WARN_AS_ERROR FAIL_ON_WARNINGS)
  set(DOXYGEN_WARN_IF_UNDOCUMENTED NO)
  # Documentation-only feature gates: include optional adapter, diagnostics,
  # panel, and WebGPU APIs in the reference without third-party headers.
  set(DOXYGEN_PREDEFINED
      "TESS_ENABLE_ENTT"
      "ENTT_VERSION=1"
      "TESS_ENABLE_FLECS"
      "FLECS_H"
      "TESS_ENABLE_IMGUI"
      "IMGUI_VERSION=1"
      "TESS_ENABLE_WEBGPU"
      "WEBGPU_H_"
      "TESS_ENABLE_DIAGNOSTICS"
      "TESS_DIAGNOSTICS_ENABLED=1")
  doxygen_add_docs(
    tess_docs
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${TESS_GENERATED_INCLUDE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/docs/api-main.md"
    COMMENT "Generating tess API reference"
  )
endif()
