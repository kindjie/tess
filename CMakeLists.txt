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
option(TESS_BUILD_BENCHMARKS "Build tess benchmarks" OFF)

add_library(tess INTERFACE)
add_library(tess::tess ALIAS tess)

target_compile_features(tess INTERFACE cxx_std_20)

target_sources(
  tess
  INTERFACE
    FILE_SET HEADERS
    BASE_DIRS include
    FILES
      include/tess/core/shape.h
      include/tess/storage/chunk_page.h
      include/tess/storage/world.h
      include/tess/tess.h
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
    add_subdirectory(tests)
  endif()
endif()

if(TESS_BUILD_BENCHMARKS)
  add_subdirectory(bench)
endif()
