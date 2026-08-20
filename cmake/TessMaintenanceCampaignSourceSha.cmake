# Resolves the source identity embedded into the maintenance campaign
# benchmark without requiring a Git checkout.
#
# Precedence: an explicit cache-provided TESS_MAINTENANCE_CAMPAIGN_SOURCE_SHA
# (exactly 40 lowercase hex digits) wins; otherwise Git resolves HEAD when
# both the tool and a repository are present; otherwise a clearly
# non-admissible sentinel is embedded so ordinary benchmark configuration
# succeeds without .git. Evidence staging stays fail-closed either way:
# tools/maintenance_campaign.py rejects any source_sha that is not exactly
# 40 hex digits and additionally requires it to match the frozen build
# manifest, so a sentinel-carrying binary can never be staged as evidence.

set(
  TESS_MAINTENANCE_CAMPAIGN_SOURCE_SHA
  ""
  CACHE STRING
  "Explicit 40-hex maintenance-campaign source commit (empty: use Git)"
)

set(
  TESS_MAINTENANCE_CAMPAIGN_SOURCE_SHA_SENTINEL
  "non-admissible-source-identity-unavailable"
)

# CMake's regex grammar has no bounded repetition ({40}), so the exact
# 40-hex-digit pattern is spelled out via string(REPEAT).
string(REPEAT "[0-9a-f]" 40 _tess_campaign_hex40)
set(TESS_MAINTENANCE_CAMPAIGN_SHA_REGEX "^${_tess_campaign_hex40}$")
unset(_tess_campaign_hex40)

function(tess_resolve_maintenance_campaign_source_sha out_var source_dir)
  if(NOT TESS_MAINTENANCE_CAMPAIGN_SOURCE_SHA STREQUAL "")
    if(NOT TESS_MAINTENANCE_CAMPAIGN_SOURCE_SHA MATCHES
       "${TESS_MAINTENANCE_CAMPAIGN_SHA_REGEX}")
      message(
        FATAL_ERROR
        "TESS_MAINTENANCE_CAMPAIGN_SOURCE_SHA must be exactly 40 lowercase "
        "hex digits: \"${TESS_MAINTENANCE_CAMPAIGN_SOURCE_SHA}\""
      )
    endif()
    set("${out_var}" "${TESS_MAINTENANCE_CAMPAIGN_SOURCE_SHA}" PARENT_SCOPE)
    return()
  endif()
  find_package(Git QUIET)
  if(GIT_FOUND)
    execute_process(
      COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
      WORKING_DIRECTORY "${source_dir}"
      RESULT_VARIABLE tess_campaign_git_status
      OUTPUT_VARIABLE tess_campaign_git_head
      ERROR_QUIET
      OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    # A SHA-256 repository or unexpected output falls through to the
    # sentinel: staging accepts only 40-hex identities.
    if(tess_campaign_git_status EQUAL 0
       AND tess_campaign_git_head MATCHES
           "${TESS_MAINTENANCE_CAMPAIGN_SHA_REGEX}")
      set("${out_var}" "${tess_campaign_git_head}" PARENT_SCOPE)
      return()
    endif()
  endif()
  message(
    STATUS
    "Maintenance-campaign source identity is unavailable without Git; "
    "embedding the non-admissible sentinel"
  )
  set(
    "${out_var}"
    "${TESS_MAINTENANCE_CAMPAIGN_SOURCE_SHA_SENTINEL}"
    PARENT_SCOPE
  )
endfunction()
