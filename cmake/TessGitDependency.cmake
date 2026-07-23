include_guard(GLOBAL)

include(FetchContent)
include("${CMAKE_CURRENT_LIST_DIR}/TessCMakeCompatibility.cmake")

function(tess_declare_git_dependency dependency repository revision)
  if(NOT dependency MATCHES "^[a-z0-9_]+$")
    message(FATAL_ERROR "Invalid dependency name: ${dependency}")
  endif()
  string(LENGTH "${revision}" revision_length)
  if(NOT revision MATCHES "^[0-9a-f]+$" OR NOT revision_length EQUAL 40)
    message(FATAL_ERROR "${dependency} revision must be a full commit SHA")
  endif()

  find_package(Git REQUIRED)
  FetchContent_Declare(
    "${dependency}"
    DOWNLOAD_COMMAND
      "${CMAKE_COMMAND}"
      "-DTESS_GIT_EXECUTABLE=${GIT_EXECUTABLE}"
      "-DTESS_GIT_DEPENDENCY=${dependency}"
      "-DTESS_GIT_REPOSITORY=${repository}"
      "-DTESS_GIT_REVISION=${revision}"
      "-DTESS_GIT_SOURCE_DIR=<SOURCE_DIR>"
      -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/TessGitPopulate.cmake"
    SYSTEM
    ${TESS_FETCHCONTENT_EXCLUDE_FROM_ALL}
  )
endfunction()
