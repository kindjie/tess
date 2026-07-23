cmake_minimum_required(VERSION 3.25)

foreach(
  required
  IN ITEMS
    TESS_GIT_EXECUTABLE
    TESS_GIT_DEPENDENCY
    TESS_GIT_REPOSITORY
    TESS_GIT_REVISION
    TESS_GIT_SOURCE_DIR
)
  if(NOT DEFINED "${required}" OR "${${required}}" STREQUAL "")
    message(FATAL_ERROR "Missing required variable: ${required}")
  endif()
endforeach()

if(NOT TESS_GIT_DEPENDENCY MATCHES "^[a-z0-9_]+$")
  message(FATAL_ERROR "Invalid dependency name: ${TESS_GIT_DEPENDENCY}")
endif()
if(NOT TESS_GIT_REPOSITORY MATCHES "^https://")
  message(FATAL_ERROR "Dependency repository must use HTTPS")
endif()
string(LENGTH "${TESS_GIT_REVISION}" revision_length)
if(
  NOT TESS_GIT_REVISION MATCHES "^[0-9a-f]+$"
  OR NOT revision_length EQUAL 40
)
  message(FATAL_ERROR "Dependency revision must be a full commit SHA")
endif()

cmake_path(IS_ABSOLUTE TESS_GIT_SOURCE_DIR source_is_absolute)
cmake_path(GET TESS_GIT_SOURCE_DIR FILENAME source_name)
cmake_path(GET TESS_GIT_SOURCE_DIR PARENT_PATH source_parent)
if(NOT source_is_absolute)
  message(FATAL_ERROR "Dependency source directory must be absolute")
endif()
if(NOT source_name STREQUAL "${TESS_GIT_DEPENDENCY}-src")
  message(FATAL_ERROR "Refusing unexpected dependency source directory")
endif()
if(source_parent STREQUAL "" OR source_parent STREQUAL "/")
  message(FATAL_ERROR "Refusing dependency source directory at filesystem root")
endif()

# When this script runs under a Git hook (pre-push builds), Git exports its
# hook environment; an inherited GIT_DIR redirects every command below at
# the parent project's repository instead of the dependency source
# directory. The list is `git rev-parse --local-env-vars`: every variable
# that scopes Git to a particular repository or injects its configuration.
foreach(
  hook_variable
  IN ITEMS
    GIT_ALTERNATE_OBJECT_DIRECTORIES
    GIT_CONFIG
    GIT_CONFIG_PARAMETERS
    GIT_CONFIG_COUNT
    GIT_OBJECT_DIRECTORY
    GIT_DIR
    GIT_WORK_TREE
    GIT_IMPLICIT_WORK_TREE
    GIT_GRAFT_FILE
    GIT_INDEX_FILE
    GIT_NO_REPLACE_OBJECTS
    GIT_REPLACE_REF_BASE
    GIT_PREFIX
    GIT_SHALLOW_FILE
    GIT_COMMON_DIR
)
  unset(ENV{${hook_variable}})
endforeach()

set(populate_attempts 3)
set(attempt_errors "")
macro(tess_populate_attempt_failed message)
  string(APPEND attempt_errors "\n  attempt ${attempt}: ${message}")
endmacro()
foreach(attempt RANGE 1 ${populate_attempts})
  file(REMOVE_RECURSE "${TESS_GIT_SOURCE_DIR}")
  file(MAKE_DIRECTORY "${TESS_GIT_SOURCE_DIR}")

  execute_process(
    COMMAND
      "${TESS_GIT_EXECUTABLE}" init --quiet "${TESS_GIT_SOURCE_DIR}"
    RESULT_VARIABLE result
    ERROR_VARIABLE error
  )
  if(NOT result EQUAL 0)
    tess_populate_attempt_failed("git init failed: ${error}")
    continue()
  endif()

  execute_process(
    COMMAND
      "${TESS_GIT_EXECUTABLE}" -C "${TESS_GIT_SOURCE_DIR}"
      remote add origin "${TESS_GIT_REPOSITORY}"
    RESULT_VARIABLE result
    ERROR_VARIABLE error
  )
  if(NOT result EQUAL 0)
    tess_populate_attempt_failed("git remote add failed: ${error}")
    continue()
  endif()

  execute_process(
    COMMAND
      "${TESS_GIT_EXECUTABLE}" -C "${TESS_GIT_SOURCE_DIR}"
      fetch --no-tags --depth=1 origin "${TESS_GIT_REVISION}"
    RESULT_VARIABLE result
    OUTPUT_QUIET
    ERROR_VARIABLE error
  )
  if(NOT result EQUAL 0)
    tess_populate_attempt_failed("git fetch failed: ${error}")
    continue()
  endif()

  execute_process(
    COMMAND
      "${TESS_GIT_EXECUTABLE}" -C "${TESS_GIT_SOURCE_DIR}"
      rev-parse --verify FETCH_HEAD
    RESULT_VARIABLE result
    OUTPUT_VARIABLE fetched_revision
    ERROR_VARIABLE error
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  if(NOT result EQUAL 0 OR NOT fetched_revision STREQUAL TESS_GIT_REVISION)
    tess_populate_attempt_failed(
      "git fetch returned an unexpected revision: ${error}"
    )
    continue()
  endif()

  execute_process(
    COMMAND
      "${TESS_GIT_EXECUTABLE}" -C "${TESS_GIT_SOURCE_DIR}"
      checkout --quiet --detach "${TESS_GIT_REVISION}" --
    RESULT_VARIABLE result
    ERROR_VARIABLE error
  )
  if(NOT result EQUAL 0)
    tess_populate_attempt_failed("git checkout failed: ${error}")
    continue()
  endif()

  execute_process(
    COMMAND
      "${TESS_GIT_EXECUTABLE}" -C "${TESS_GIT_SOURCE_DIR}"
      rev-parse --verify HEAD
    RESULT_VARIABLE result
    OUTPUT_VARIABLE checked_out_revision
    ERROR_VARIABLE error
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  if(result EQUAL 0 AND checked_out_revision STREQUAL TESS_GIT_REVISION)
    message(
      STATUS
      "Populated ${TESS_GIT_DEPENDENCY} at ${TESS_GIT_REVISION} "
      "on attempt ${attempt}"
    )
    return()
  endif()
  tess_populate_attempt_failed("git checkout verification failed: ${error}")
endforeach()

message(
  FATAL_ERROR
  "Failed to populate ${TESS_GIT_DEPENDENCY} after "
  "${populate_attempts} attempts:${attempt_errors}"
)
