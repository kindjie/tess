cmake_minimum_required(VERSION 3.25)

include(CMakePackageConfigHelpers)

if(NOT DEFINED TESS_SOURCE_DIR OR NOT DEFINED TESS_TEST_ROOT)
  message(FATAL_ERROR "TESS_SOURCE_DIR and TESS_TEST_ROOT are required")
endif()

file(REMOVE_RECURSE "${TESS_TEST_ROOT}")
file(MAKE_DIRECTORY "${TESS_TEST_ROOT}/consumer")
file(WRITE "${TESS_TEST_ROOT}/consumer/CMakeLists.txt" [=[
cmake_minimum_required(VERSION 3.25)
project(tess_package_selection NONE)
if(TESS_REQUEST STREQUAL "")
  find_package(tess CONFIG REQUIRED PATHS "${TESS_PACKAGE_DIR}"
               NO_DEFAULT_PATH)
else()
  find_package(tess ${TESS_REQUEST} CONFIG REQUIRED
               PATHS "${TESS_PACKAGE_DIR}" NO_DEFAULT_PATH)
endif()
if(NOT DEFINED tess_VERSION OR
   NOT "${tess_VERSION}" STREQUAL "${TESS_EXPECT_VERSION}")
  message(FATAL_ERROR "discovered package version does not match")
endif()
]=])

function(make_package name version prerelease compatibility)
  set(package_dir "${TESS_TEST_ROOT}/${name}/package")
  file(MAKE_DIRECTORY "${package_dir}")
  file(WRITE "${package_dir}/tess-targets.cmake" "")
  set(TESS_VERSION "${version}")
  set(TESS_VERSION_PRERELEASE "${prerelease}")
  set(TESS_VERSION_STRING "${version}")
  if(NOT prerelease STREQUAL "")
    string(APPEND TESS_VERSION_STRING "-${prerelease}")
  endif()
  configure_package_config_file(
    "${TESS_SOURCE_DIR}/cmake/tess-config.cmake.in"
    "${package_dir}/tess-config.cmake"
    INSTALL_DESTINATION lib/cmake/tess
  )
  write_basic_package_version_file(
    "${package_dir}/tess-config-version-base.cmake"
    VERSION "${version}"
    COMPATIBILITY "${compatibility}"
    ARCH_INDEPENDENT
  )
  configure_file(
    "${TESS_SOURCE_DIR}/cmake/tess-config-version.cmake.in"
    "${package_dir}/tess-config-version.cmake"
    @ONLY
  )
endfunction()

function(check_selection name package_name request expect_success)
  set(build_dir "${TESS_TEST_ROOT}/${name}/build")
  execute_process(
    COMMAND "${CMAKE_COMMAND}"
            -S "${TESS_TEST_ROOT}/consumer"
            -B "${build_dir}"
            "-DTESS_PACKAGE_DIR=${TESS_TEST_ROOT}/${package_name}/package"
            "-DTESS_REQUEST=${request}"
            "-DTESS_EXPECT_VERSION=${ARGN}"
    RESULT_VARIABLE result
    OUTPUT_QUIET
    ERROR_QUIET
  )
  if(expect_success AND NOT result EQUAL 0)
    message(FATAL_ERROR "${name}: expected package selection to succeed")
  endif()
  if(NOT expect_success AND result EQUAL 0)
    message(FATAL_ERROR "${name}: expected package selection to fail")
  endif()
endfunction()

make_package(v0_13 0.13.2 "" SameMinorVersion)
check_selection(v0_same_minor v0_13 0.13.0 TRUE 0.13.2)
check_selection(v0_other_minor v0_13 0.14.0 FALSE 0.13.2)

make_package(v1_rc 1.0.0 rc.1 SameMajorVersion)
check_selection(rc_unversioned v1_rc "" TRUE 1.0.0)
check_selection(rc_stable_request v1_rc 1.0.0 FALSE 1.0.0)

make_package(v1_3 1.3.0 "" SameMajorVersion)
check_selection(v1_later_minor v1_3 1.1.0 TRUE 1.3.0)

make_package(v1_0 1.0.0 "" SameMajorVersion)
check_selection(v1_too_old v1_0 1.1.0 FALSE 1.0.0)

make_package(v2_0 2.0.0 "" SameMajorVersion)
check_selection(v1_incompatible_major v2_0 1.0.0 FALSE 2.0.0)
