cmake_minimum_required(VERSION 3.25)

project(tess_install_consumer LANGUAGES CXX)

option(TESS_NO_EXCEPTIONS "Compile this consumer without exceptions" OFF)

if(NOT DEFINED TESS_EXPECTED_VERSION)
  message(FATAL_ERROR "TESS_EXPECTED_VERSION is required")
endif()

find_package(tess ${TESS_EXPECTED_VERSION} EXACT CONFIG REQUIRED)
if(NOT tess_VERSION VERSION_EQUAL TESS_EXPECTED_VERSION)
  message(FATAL_ERROR "installed tess version does not match")
endif()

string(REPLACE "." ";" version_parts "${TESS_EXPECTED_VERSION}")
list(GET version_parts 0 version_major)
list(GET version_parts 1 version_minor)
list(GET version_parts 2 version_patch)

add_executable(tess_install_consumer main.cc)
target_link_libraries(tess_install_consumer PRIVATE tess::tess)
target_compile_definitions(
  tess_install_consumer
  PRIVATE
    TESS_EXPECTED_VERSION_MAJOR=${version_major}
    TESS_EXPECTED_VERSION_MINOR=${version_minor}
    TESS_EXPECTED_VERSION_PATCH=${version_patch}
)
if(TESS_NO_EXCEPTIONS)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    message(FATAL_ERROR "TESS_NO_EXCEPTIONS requires Clang-family or GCC")
  endif()
  target_compile_options(tess_install_consumer PRIVATE -fno-exceptions)
  target_compile_definitions(
    tess_install_consumer PRIVATE TESS_EXPECT_NO_EXCEPTIONS=1)
endif()
