include_guard(DIRECTORY)

include("${CMAKE_CURRENT_LIST_DIR}/TessGitDependency.cmake")

# Commit SHA pinned so upstream tag moves cannot alter builds.
set(TESS_ENTT_REVISION
    "b4e58bdd364ad72246c123a0c28538eab3252672") # tag v3.16.0
set(TESS_ENTT_MIN_VERSION "3.16.0")

function(tess_require_entt)
  if(TARGET EnTT::EnTT)
    message(
      STATUS
      "Using trusted pre-existing EnTT::EnTT; version validation is the "
      "parent project's responsibility"
    )
    return()
  endif()

  if(TESS_USE_SYSTEM_DEPENDENCIES)
    find_package(EnTT ${TESS_ENTT_MIN_VERSION} CONFIG REQUIRED)
    if(NOT TARGET EnTT::EnTT)
      message(FATAL_ERROR "EnTT did not provide expected target EnTT::EnTT")
    endif()
    return()
  endif()

  tess_declare_git_dependency(
    entt
    https://github.com/skypjack/entt.git
    "${TESS_ENTT_REVISION}"
  )
  FetchContent_MakeAvailable(entt)

  if(NOT TARGET EnTT::EnTT)
    message(FATAL_ERROR "EnTT did not provide expected target EnTT::EnTT")
  endif()
endfunction()
