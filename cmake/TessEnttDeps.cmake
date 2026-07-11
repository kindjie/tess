include_guard(DIRECTORY)

include(FetchContent)

# Commit SHA pinned so upstream tag moves cannot alter builds. This is the
# downstream consumer's known-good, MSVC-exercised EnTT pin; keep the two
# repositories' pins in lockstep when upgrading.
set(TESS_ENTT_VERSION
    "b4e58bdd364ad72246c123a0c28538eab3252672") # tag v3.16.0

function(tess_require_entt)
  if(TARGET EnTT::EnTT)
    return()
  endif()

  find_package(EnTT CONFIG QUIET)
  if(TARGET EnTT::EnTT)
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
