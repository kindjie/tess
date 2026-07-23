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

  set(FLECS_SHARED OFF CACHE BOOL "Build the Flecs shared library" FORCE)
  set(FLECS_STATIC ON CACHE BOOL "Build the Flecs static library" FORCE)
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
