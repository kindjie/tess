cmake_minimum_required(VERSION 3.25)

project(tess_install_consumer LANGUAGES CXX)

option(TESS_NO_EXCEPTIONS "Compile this consumer without exceptions" OFF)

if(NOT DEFINED TESS_EXPECTED_VERSION)
  message(FATAL_ERROR "TESS_EXPECTED_VERSION is required")
endif()
if(NOT DEFINED TESS_EXPECTED_PRERELEASE)
  set(TESS_EXPECTED_PRERELEASE "")
endif()

if(TESS_EXPECTED_PRERELEASE STREQUAL "")
  find_package(tess ${TESS_EXPECTED_VERSION} EXACT CONFIG REQUIRED)
else()
  # A prerelease deliberately rejects every versioned request.
  find_package(tess CONFIG REQUIRED)
endif()
if(NOT tess_VERSION VERSION_EQUAL TESS_EXPECTED_VERSION)
  message(FATAL_ERROR "installed tess version does not match")
endif()

string(REPLACE "." ";" version_parts "${TESS_EXPECTED_VERSION}")
list(GET version_parts 0 version_major)
list(GET version_parts 1 version_minor)
list(GET version_parts 2 version_patch)
set(expected_version_string "${TESS_EXPECTED_VERSION}")
if(NOT TESS_EXPECTED_PRERELEASE STREQUAL "")
  string(APPEND expected_version_string "-${TESS_EXPECTED_PRERELEASE}")
endif()
if(NOT tess_VERSION_PRERELEASE STREQUAL TESS_EXPECTED_PRERELEASE)
  message(FATAL_ERROR "installed tess prerelease does not match")
endif()
if(NOT tess_VERSION_STRING STREQUAL expected_version_string)
  message(FATAL_ERROR "installed tess version string does not match")
endif()

add_executable(tess_install_consumer main.cc)
target_link_libraries(tess_install_consumer PRIVATE tess::tess)
target_compile_definitions(
  tess_install_consumer
  PRIVATE
    TESS_EXPECTED_VERSION_MAJOR=${version_major}
    TESS_EXPECTED_VERSION_MINOR=${version_minor}
    TESS_EXPECTED_VERSION_PATCH=${version_patch}
    TESS_EXPECTED_VERSION_PRERELEASE="${TESS_EXPECTED_PRERELEASE}"
    TESS_EXPECTED_VERSION_STRING="${expected_version_string}"
)
if(TESS_NO_EXCEPTIONS)
  if(MSVC)
    target_compile_options(tess_install_consumer PRIVATE /EHs-c-)
    target_compile_definitions(
      tess_install_consumer PRIVATE _HAS_EXCEPTIONS=0)
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(tess_install_consumer PRIVATE -fno-exceptions)
  else()
    message(FATAL_ERROR "TESS_NO_EXCEPTIONS uses an unsupported compiler")
  endif()
  target_compile_definitions(
    tess_install_consumer PRIVATE TESS_EXPECT_NO_EXCEPTIONS=1)
endif()
