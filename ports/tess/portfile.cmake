# tess is header-only: the port installs headers and the CMake package
# files, and installs no libraries. The configure step mirrors the
# consumer preset so the packaged surface matches what
# find_package(tess) provides from a plain install -- a second
# definition here would be free to drift from the real one.
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO kindjie/tess
    REF "v${VERSION}"
    # Placeholder: vcpkg hashes a published release archive, so this is
    # filled in when a version is tagged. The overlay exists to prove
    # the packaging shape below, which does not depend on the hash.
    SHA512 0
    HEAD_REF main
)

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
