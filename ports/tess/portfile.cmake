# tess is header-only: this checkout overlay installs the enclosing source
# tree's headers and CMake package files, and installs no libraries. A future
# central-registry port can acquire a tagged archive after a release exists;
# this overlay deliberately remains usable before and at the release tag.
get_filename_component(
  SOURCE_PATH "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE
)
if(NOT EXISTS "${SOURCE_PATH}/cmake/tess-version.cmake")
  message(FATAL_ERROR
    "The tess overlay must be used from the repository's ports directory"
  )
endif()

vcpkg_cmake_configure(
  SOURCE_PATH "${SOURCE_PATH}"
  OPTIONS
    -DTESS_BUILD_TESTING=OFF
    -DTESS_BUILD_EXAMPLES=OFF
    -DTESS_BUILD_BENCHMARKS=OFF
    -DTESS_BUILD_DOCS=OFF
    -DTESS_ENABLE_ENTT=OFF
    -DTESS_ENABLE_FLECS=OFF
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME tess CONFIG_PATH lib/cmake/tess)

# Header-only: no debug tree and no lib directories to keep.
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/lib")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
