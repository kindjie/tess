"""Conan 2 recipe for tess.

tess is header-only: the package carries headers and the CMake
package files, declares no libraries to link, and requires no build
step for consumers. The recipe deliberately mirrors the `consumer`
CMake preset — developer facilities off, optional integrations off —
so what Conan installs is the same surface `find_package(tess)`
installs, not a second definition of the package that could drift.
"""

import os
import re

from conan import ConanFile
from conan.tools.build import check_min_cppstd
from conan.tools.cmake import CMake, CMakeToolchain, cmake_layout
from conan.tools.files import copy


def _version_from_cmake(recipe_folder):
    """Read the single source of truth rather than restating it."""
    version_file = os.path.join(recipe_folder, "cmake", "tess-version.cmake")
    with open(version_file, encoding="utf-8") as handle:
        match = re.search(r"set\(TESS_VERSION\s+([0-9.]+)\)", handle.read())
    if match is None:
        raise RuntimeError("cannot read TESS_VERSION from tess-version.cmake")
    return match.group(1)


class TessConan(ConanFile):
    name = "tess"
    license = "MIT"
    url = "https://github.com/kindjie/tess"
    homepage = "https://github.com/kindjie/tess"
    description = "Performance-first tile and path simulation substrate"
    topics = ("gamedev", "pathfinding", "simulation", "header-only")

    package_type = "header-library"
    settings = "os", "arch", "compiler", "build_type"
    no_copy_source = True

    exports_sources = (
        "CMakeLists.txt",
        "cmake/*",
        "include/*",
        "LICENSE",
    )

    def set_version(self):
        self.version = self.version or _version_from_cmake(self.recipe_folder)

    def validate(self):
        # tess is C++20 throughout; fail at graph time with a clear
        # message rather than deep in a template error.
        check_min_cppstd(self, 20)

    def layout(self):
        cmake_layout(self)

    def package_id(self):
        # Header-only: the package contents do not depend on the
        # consumer's compiler or build type.
        self.info.clear()

    def generate(self):
        toolchain = CMakeToolchain(self)
        # Match the consumer preset: no tests, examples, benchmarks,
        # docs, or optional adapters in a packaged install.
        toolchain.cache_variables["TESS_BUILD_TESTING"] = False
        toolchain.cache_variables["TESS_BUILD_EXAMPLES"] = False
        toolchain.cache_variables["TESS_BUILD_BENCHMARKS"] = False
        toolchain.cache_variables["TESS_BUILD_DOCS"] = False
        toolchain.cache_variables["TESS_ENABLE_ENTT"] = False
        toolchain.cache_variables["TESS_ENABLE_FLECS"] = False
        toolchain.generate()

    def build(self):
        # Nothing to compile; configure still runs so the generated
        # version header and package files exist to install.
        cmake = CMake(self)
        cmake.configure()

    def package(self):
        cmake = CMake(self)
        cmake.install()
        copy(
            self,
            "LICENSE",
            src=self.source_folder,
            dst=os.path.join(self.package_folder, "licenses"),
        )

    def package_info(self):
        self.cpp_info.bindirs = []
        self.cpp_info.libdirs = []
        # Consumers get the same imported target the installed CMake
        # package provides.
        self.cpp_info.set_property("cmake_file_name", "tess")
        self.cpp_info.set_property("cmake_target_name", "tess::tess")
