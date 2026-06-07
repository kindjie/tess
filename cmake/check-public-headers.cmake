if(NOT DEFINED TESS_SOURCE_DIR)
  message(FATAL_ERROR "TESS_SOURCE_DIR is required")
endif()
if(NOT DEFINED TESS_DECLARED_HEADERS)
  message(FATAL_ERROR "TESS_DECLARED_HEADERS is required")
endif()

file(GLOB_RECURSE actual_headers
  RELATIVE "${TESS_SOURCE_DIR}"
  "${TESS_SOURCE_DIR}/include/tess/*.h"
)
list(SORT actual_headers)

set(declared_headers ${TESS_DECLARED_HEADERS})
list(SORT declared_headers)

set(missing_headers ${actual_headers})
list(REMOVE_ITEM missing_headers ${declared_headers})

set(stale_headers ${declared_headers})
list(REMOVE_ITEM stale_headers ${actual_headers})

if(missing_headers OR stale_headers)
  if(missing_headers)
    list(JOIN missing_headers "\n  " missing_text)
    message(SEND_ERROR "Headers missing from TESS_PUBLIC_HEADERS:\n  ${missing_text}")
  endif()
  if(stale_headers)
    list(JOIN stale_headers "\n  " stale_text)
    message(SEND_ERROR "Headers listed in TESS_PUBLIC_HEADERS but not found:\n  ${stale_text}")
  endif()
  message(FATAL_ERROR "Public header file set is out of sync")
endif()
