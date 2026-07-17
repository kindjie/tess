include_guard(DIRECTORY)

include(FetchContent)

# Commit SHA pinned so upstream tag moves cannot alter builds.
set(TESS_ENTT_VERSION
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

  FetchContent_Declare(
    entt
    GIT_REPOSITORY https://github.com/skypjack/entt.git
    GIT_TAG "${TESS_ENTT_VERSION}"
    SYSTEM
    EXCLUDE_FROM_ALL
  )
  FetchContent_MakeAvailable(entt)

  if(NOT TARGET EnTT::EnTT)
    message(FATAL_ERROR "EnTT did not provide expected target EnTT::EnTT")
  endif()
endfunction()
