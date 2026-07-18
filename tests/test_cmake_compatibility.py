"""Regression tests for the supported CMake compatibility floor."""

from __future__ import annotations

import subprocess
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
COMPATIBILITY_MODULE = REPO_ROOT / "cmake" / "TessCMakeCompatibility.cmake"


def run_compatibility_probe(tmp_path: Path, version: str) -> str:
    script = tmp_path / f"probe-{version}.cmake"
    script.write_text(
        f'set(CMAKE_VERSION "{version}")\n'
        f'include("{COMPATIBILITY_MODULE.as_posix()}")\n'
        'message(STATUS "module-scan=${TESS_CMAKE_HAS_MODULE_SCAN}")\n'
        'message(STATUS "fetch-exclude='
        '${TESS_FETCHCONTENT_EXCLUDE_FROM_ALL}")\n',
        encoding="utf-8",
    )
    result = subprocess.run(
        ["cmake", "-P", str(script)],
        check=True,
        capture_output=True,
        text=True,
    )
    return result.stdout + result.stderr


def test_cmake_325_omits_328_only_options(tmp_path):
    output = run_compatibility_probe(tmp_path, "3.25.0")

    assert "module-scan=OFF" in output
    assert "fetch-exclude=" in output
    assert "fetch-exclude=EXCLUDE_FROM_ALL" not in output


def test_cmake_328_retains_newer_build_hygiene(tmp_path):
    output = run_compatibility_probe(tmp_path, "3.28.0")

    assert "module-scan=ON" in output
    assert "fetch-exclude=EXCLUDE_FROM_ALL" in output


def test_project_and_presets_declare_the_supported_floor():
    cmake_lists = (REPO_ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    presets = (REPO_ROOT / "CMakePresets.json").read_text(encoding="utf-8")

    assert cmake_lists.startswith("cmake_minimum_required(VERSION 3.25)\n")
    assert '"minor": 25' in presets
