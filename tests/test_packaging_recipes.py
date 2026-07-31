"""Tests for the Conan recipe and vcpkg overlay port.

A version or option set restated in three places drifts silently, and
the failure surfaces as a broken package for a consumer rather than a
red build here. These tests pin every restatement against the single
source of truth.
"""

from __future__ import annotations

import json
import re
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]

# The options a packaged install must set, matching the consumer
# preset: no developer facilities and no optional integrations.
CONSUMER_OPTIONS = (
  "TESS_BUILD_TESTING",
  "TESS_BUILD_EXAMPLES",
  "TESS_BUILD_BENCHMARKS",
  "TESS_BUILD_DOCS",
  "TESS_ENABLE_ENTT",
  "TESS_ENABLE_FLECS",
)


def declared_version() -> str:
  text = (REPO / "cmake" / "tess-version.cmake").read_text(encoding="utf-8")
  match = re.search(r"set\(TESS_VERSION\s+([0-9.]+)\)", text)
  assert match is not None, "TESS_VERSION not found"
  return match.group(1)


def test_vcpkg_port_version_matches_the_project():
  manifest = json.loads(
    (REPO / "ports" / "tess" / "vcpkg.json").read_text(encoding="utf-8")
  )

  assert manifest["version"] == declared_version()


def test_conan_recipe_reads_the_version_rather_than_restating_it():
  recipe = (REPO / "conanfile.py").read_text(encoding="utf-8")

  # The recipe must derive the version from tess-version.cmake. A
  # literal would be a fourth place to forget.
  assert "tess-version.cmake" in recipe
  assert not re.search(r'version\s*=\s*"[0-9]+\.[0-9]+\.[0-9]+"', recipe)


def test_both_recipes_configure_the_consumer_option_set():
  recipe = (REPO / "conanfile.py").read_text(encoding="utf-8")
  portfile = (
    REPO / "ports" / "tess" / "portfile.cmake"
  ).read_text(encoding="utf-8")

  for option in CONSUMER_OPTIONS:
    assert option in recipe, f"conan recipe does not set {option}"
    assert f"-D{option}=OFF" in portfile, f"portfile does not set {option}"


def test_vcpkg_port_declares_header_only_layout():
  portfile = (
    REPO / "ports" / "tess" / "portfile.cmake"
  ).read_text(encoding="utf-8")

  # A header-only port must remove the debug tree and any lib
  # directory, or vcpkg's post-build checks fail the port.
  assert 'file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug")' in portfile
  assert 'file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/lib")' in portfile
  assert "vcpkg_install_copyright" in portfile


def test_conan_recipe_declares_header_library():
  recipe = (REPO / "conanfile.py").read_text(encoding="utf-8")

  assert 'package_type = "header-library"' in recipe
  # Header-only packages must not vary by compiler or build type.
  assert "self.info.clear()" in recipe
  assert 'set_property("cmake_target_name", "tess::tess")' in recipe


def test_recipes_are_listed_in_the_packaging_document():
  packaging = (REPO / "docs" / "packaging.md").read_text(encoding="utf-8")

  # The document previously said no recipe existed; it must not still
  # say that now that both are in the tree.
  assert "conanfile.py" in packaging
  assert "ports/tess" in packaging
