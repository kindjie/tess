cmake_minimum_required(VERSION 3.25...3.28)

include(cmake/tess-version.cmake)
include(cmake/TessCMakeCompatibility.cmake)
include(cmake/TessHeaderManifest.cmake)

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
option(
  TESS_BUILD_NO_EXCEPTIONS_TESTING
  "Build exception-free compiler-mode contract targets"
  OFF
)
option(TESS_BUILD_EXAMPLES "Build tess examples" ${PROJECT_IS_TOP_LEVEL})
option(TESS_BUILD_BENCHMARKS "Build tess benchmarks" OFF)
option(TESS_BUILD_FUZZING "Build libFuzzer archive targets" OFF)
option(TESS_BUILD_DOCS "Build the tess API reference with Doxygen" OFF)
set(
  TESS_DOC_VERSION_LABEL
  "${TESS_VERSION_STRING}"
  CACHE STRING
  "Exact version label shown in generated documentation"
)
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

# Header classes come from cmake/tess-headers.json. Public and implementation
# headers live in separate file sets so the
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
    FILES ${TESS_STABLE_SOURCE_HEADERS}
  INTERFACE
    FILE_SET optional_stable_headers
    TYPE HEADERS
    BASE_DIRS include
    FILES ${TESS_OPTIONAL_STABLE_HEADERS}
  INTERFACE
    FILE_SET experimental_headers
    TYPE HEADERS
    BASE_DIRS include
    FILES ${TESS_EXPERIMENTAL_HEADERS}
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
    INTERFACE_HEADER_SETS_TO_VERIFY
      "HEADERS;optional_stable_headers;experimental_headers;generated_headers"
)

# A build-only mirror of the public header contract under the real compiler
# exception-disable flag. The installed tess target stays policy-neutral.
if(TESS_BUILD_TESTING AND TESS_BUILD_NO_EXCEPTIONS_TESTING AND
   (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU" OR MSVC))
  add_library(tess_no_exceptions_headers INTERFACE)
  target_link_libraries(tess_no_exceptions_headers INTERFACE tess::tess)
  tess_target_disable_exceptions(tess_no_exceptions_headers)
  target_sources(
    tess_no_exceptions_headers
    INTERFACE
      FILE_SET no_exceptions_headers
      TYPE HEADERS
      BASE_DIRS include
      FILES ${TESS_PUBLIC_SOURCE_HEADERS}
    INTERFACE
      FILE_SET no_exceptions_generated_headers
      TYPE HEADERS
      BASE_DIRS "${TESS_GENERATED_INCLUDE_DIR}"
      FILES "${TESS_GENERATED_INCLUDE_DIR}/tess/version.h"
  )
  set_target_properties(
    tess_no_exceptions_headers
    PROPERTIES
      EXCLUDE_FROM_ALL TRUE
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
  FILE_SET optional_stable_headers
  FILE_SET experimental_headers
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

if(PROJECT_VERSION_MAJOR EQUAL 0)
  set(TESS_PACKAGE_COMPATIBILITY SameMinorVersion)
else()
  set(TESS_PACKAGE_COMPATIBILITY SameMajorVersion)
endif()

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/tess-config-version-base.cmake"
  VERSION "${PROJECT_VERSION}"
  COMPATIBILITY ${TESS_PACKAGE_COMPATIBILITY}
  ARCH_INDEPENDENT
)
configure_file(
  cmake/tess-config-version.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/tess-config-version.cmake"
  @ONLY
)

install(
  FILES
    "${CMAKE_CURRENT_BINARY_DIR}/tess-config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/tess-config-version.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/tess-config-version-base.cmake"
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
        "-DTESS_DECLARED_HEADERS=$<JOIN:${TESS_INSTALL_SOURCE_HEADERS},;>"
        -P ${PROJECT_SOURCE_DIR}/cmake/check-public-headers.cmake
    )
    # Seconds-cheap install-contract check; runs on every selective
    # pre-push (the -L selection would otherwise exclude it).
    set_tests_properties(
      tess_installed_headers_file_set
      PROPERTIES LABELS "prepush:always"
    )
    add_test(
      NAME tess_package_version_selection
      COMMAND
        "${CMAKE_COMMAND}"
        -DTESS_SOURCE_DIR=${PROJECT_SOURCE_DIR}
        -DTESS_TEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/package-version-test
        -P ${PROJECT_SOURCE_DIR}/tests/cmake/test-package-version.cmake
    )
    set_tests_properties(
      tess_package_version_selection
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

if(TESS_BUILD_FUZZING)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "TESS_BUILD_FUZZING requires Clang libFuzzer")
  endif()
  add_executable(
    tess_world_archive_fuzzer
    tests/fuzz/tess_world_archive_fuzzer.cc
  )
  target_link_libraries(tess_world_archive_fuzzer PRIVATE tess::tess)
  target_compile_options(
    tess_world_archive_fuzzer PRIVATE -fsanitize=fuzzer,address,undefined
  )
  target_link_options(
    tess_world_archive_fuzzer PRIVATE -fsanitize=fuzzer,address,undefined
  )
  tess_apply_project_options(tess_world_archive_fuzzer)
endif()

if(TESS_BUILD_DOCS)
  find_package(Doxygen REQUIRED)
  set(DOXYGEN_PROJECT_NUMBER "${TESS_DOC_VERSION_LABEL}")
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
