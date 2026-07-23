include_guard(DIRECTORY)

include(FetchContent)
include("${CMAKE_CURRENT_LIST_DIR}/TessCMakeCompatibility.cmake")

# Commit archive and hash pinned so upstream tag moves cannot alter builds.
set(TESS_ENTT_REVISION
    "b4e58bdd364ad72246c123a0c28538eab3252672") # tag v3.16.0
set(TESS_ENTT_MIN_VERSION "3.16.0")
string(
  CONCAT TESS_ENTT_URL
  "https://github.com/skypjack/entt/archive/"
  "${TESS_ENTT_REVISION}.tar.gz"
)
string(
  CONCAT TESS_ENTT_FALLBACK_URL
  "https://codeload.github.com/skypjack/entt/tar.gz/"
  "${TESS_ENTT_REVISION}"
)

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
    URL "${TESS_ENTT_URL}" "${TESS_ENTT_FALLBACK_URL}"
    URL_HASH
      SHA256=3e996cf255b09527faf995c7c36e3daf92cefa0fb37540a4b9c359af557e19cd
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
    TLS_VERIFY TRUE
    SYSTEM
    ${TESS_FETCHCONTENT_EXCLUDE_FROM_ALL}
  )
  FetchContent_MakeAvailable(entt)

  if(NOT TARGET EnTT::EnTT)
    message(FATAL_ERROR "EnTT did not provide expected target EnTT::EnTT")
  endif()
endfunction()
