# Load the single source of truth for installed-header stability classes.
# CMake 3.25 provides the string(JSON) operations used here.
set(TESS_HEADER_MANIFEST
    "${CMAKE_CURRENT_LIST_DIR}/tess-headers.json")
file(READ "${TESS_HEADER_MANIFEST}" _tess_header_manifest_json)

function(tess_read_header_class output category)
  string(JSON _length LENGTH "${_tess_header_manifest_json}" "${category}")
  set(_headers)
  if(_length GREATER 0)
    math(EXPR _last "${_length} - 1")
    foreach(_index RANGE 0 ${_last})
      string(
        JSON _header GET "${_tess_header_manifest_json}"
        "${category}" ${_index}
      )
      list(APPEND _headers "${_header}")
    endforeach()
  endif()
  set(${output} "${_headers}" PARENT_SCOPE)
endfunction()

tess_read_header_class(TESS_STABLE_HEADERS stable)
tess_read_header_class(TESS_OPTIONAL_STABLE_HEADERS optional-stable)
tess_read_header_class(TESS_EXPERIMENTAL_HEADERS experimental)
tess_read_header_class(TESS_IMPLEMENTATION_HEADERS implementation-only)

# The installed version header is generated from its .in source. It remains in
# the manifest's stable class and all installed/compatibility inventories, but
# source-tree file sets must name the generated build-tree path separately.
set(TESS_GENERATED_MANIFEST_HEADERS "include/tess/version.h")
list(FIND TESS_STABLE_HEADERS "include/tess/version.h" _tess_version_index)
if(_tess_version_index EQUAL -1)
  message(FATAL_ERROR "generated tess/version.h must be classified stable")
endif()
set(TESS_STABLE_SOURCE_HEADERS ${TESS_STABLE_HEADERS})
list(REMOVE_ITEM TESS_STABLE_SOURCE_HEADERS ${TESS_GENERATED_MANIFEST_HEADERS})

set(TESS_PUBLIC_HEADERS
    ${TESS_STABLE_HEADERS}
    ${TESS_OPTIONAL_STABLE_HEADERS}
    ${TESS_EXPERIMENTAL_HEADERS})
set(TESS_PUBLIC_SOURCE_HEADERS ${TESS_PUBLIC_HEADERS})
list(REMOVE_ITEM TESS_PUBLIC_SOURCE_HEADERS ${TESS_GENERATED_MANIFEST_HEADERS})
set(TESS_COMPATIBILITY_HEADERS
    ${TESS_STABLE_HEADERS}
    ${TESS_OPTIONAL_STABLE_HEADERS})
set(TESS_INSTALL_HEADERS
    ${TESS_PUBLIC_HEADERS}
    ${TESS_IMPLEMENTATION_HEADERS})
set(TESS_INSTALL_SOURCE_HEADERS ${TESS_INSTALL_HEADERS})
list(REMOVE_ITEM TESS_INSTALL_SOURCE_HEADERS
     ${TESS_GENERATED_MANIFEST_HEADERS})
