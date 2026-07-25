include_guard(DIRECTORY)

include("${CMAKE_CURRENT_LIST_DIR}/TessGitDependency.cmake")

# Commit SHA pinned so upstream tag moves cannot alter builds.
set(TESS_FLECS_REVISION
    "d7d0c4f7afb4518a6bae749efdc52c7cb5cffee6") # tag v4.1.5

function(tess_require_flecs)
  if(TARGET flecs::flecs_static)
    message(
      STATUS
      "Using trusted pre-existing flecs::flecs_static; target/header "
      "consistency is the parent project's responsibility"
    )
    return()
  endif()

  if(TESS_USE_SYSTEM_DEPENDENCIES)
    # Upstream's installed package omits a ConfigVersion file. The adapter
    # enforces the minimum through Flecs' public version macros at compile
    # time instead.
    find_package(flecs CONFIG REQUIRED)
    if(NOT TARGET flecs::flecs_static)
      message(FATAL_ERROR
              "Flecs did not provide expected target flecs::flecs_static")
    endif()
    return()
  endif()

  # Seed upstream defaults without overwriting a parent project's cache.
  # tess requires the static target, but embedding consumers may deliberately
  # request both variants or supply their own compatible configuration.
  set(FLECS_SHARED OFF CACHE BOOL "Build the Flecs shared library")
  set(FLECS_STATIC ON CACHE BOOL "Build the Flecs static library")
  if(NOT FLECS_STATIC)
    message(
      FATAL_ERROR
      "TESS_ENABLE_FLECS requires FLECS_STATIC=ON or a trusted "
      "pre-existing flecs::flecs_static target"
    )
  endif()
  tess_declare_git_dependency(
    flecs
    https://github.com/SanderMertens/flecs.git
    "${TESS_FLECS_REVISION}"
  )
  FetchContent_MakeAvailable(flecs)

  if(NOT TARGET flecs::flecs_static)
    message(FATAL_ERROR
            "Flecs did not provide expected target flecs::flecs_static")
  endif()
endfunction()
