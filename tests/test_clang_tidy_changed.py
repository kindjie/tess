"""Tests for the diff-scoped clang-tidy runner."""

from __future__ import annotations

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

import clang_tidy_changed  # noqa: E402


def test_candidate_selection_splits_sources_and_headers():
  candidates = clang_tidy_changed.select_candidates(
    (
      "include/tess/ops/queued.h",
      "tests/path_test_util.h",
      "tests/tess_shape_test.cc",
      "examples/quickstart.cc",
      "bench/path_bench.cc",
      "bench/CMakeLists.txt",
      "tests/webgpu_stub/webgpu.h",
      "include/tess/version.h.in",
      "docs/index.md",
      "tools/ci_changes.py",
      "CMakeLists.txt",
    )
  )

  assert candidates.headers == (
    "include/tess/ops/queued.h",
    "tests/path_test_util.h",
  )
  assert candidates.sources == (
    "tests/tess_shape_test.cc",
    "examples/quickstart.cc",
  )


def test_candidate_selection_is_stable_and_deduplicated():
  candidates = clang_tidy_changed.select_candidates(
    (
      "include/tess/tess.h",
      "include/tess/core/shape.h",
      "include/tess/tess.h",
    )
  )

  assert candidates.headers == (
    "include/tess/tess.h",
    "include/tess/core/shape.h",
  )
  assert candidates.sources == ()


def test_reference_flags_strip_compile_only_arguments():
  entry = {
    "directory": "/repo/build/dev",
    "command": (
      "/usr/bin/clang++ -I/repo/include -isystem /deps/entt "
      "-g -std=gnu++20 -o tests/foo.cc.o -c /repo/tests/foo.cc"
    ),
    "file": "/repo/tests/foo.cc",
  }

  flags = clang_tidy_changed.reference_compile_flags(entry)

  assert flags == (
    "-I/repo/include",
    "-isystem",
    "/deps/entt",
    "-g",
    "-std=gnu++20",
  )


def test_reference_flags_support_arguments_form():
  entry = {
    "directory": "/repo/build/dev",
    "arguments": [
      "clang++",
      "-I/repo/include",
      "-std=gnu++20",
      "-c",
      "/repo/tests/foo.cc",
      "-o",
      "tests/foo.cc.o",
    ],
    "file": "/repo/tests/foo.cc",
  }

  flags = clang_tidy_changed.reference_compile_flags(entry)

  assert flags == ("-I/repo/include", "-std=gnu++20")


def test_header_check_synthesizes_absolute_include_tu(tmp_path):
  repo = tmp_path
  (repo / "include" / "tess").mkdir(parents=True)
  header = repo / "include" / "tess" / "widget.h"
  header.write_text("#pragma once\n", encoding="utf-8")

  check = clang_tidy_changed.header_check(
    "include/tess/widget.h",
    repo_root=repo,
    scratch_dir=repo / "scratch",
    flags=("-I/repo/include", "-std=gnu++20"),
    clang_tidy="clang-tidy",
  )

  tu = Path(check.tu_path)
  assert tu.read_text(encoding="utf-8") == f'#include "{header}"\n'
  assert check.command == (
    "clang-tidy",
    "--quiet",
    str(tu),
    "--",
    "-I/repo/include",
    "-std=gnu++20",
  )


def test_source_check_uses_compilation_database():
  check = clang_tidy_changed.source_check(
    "tests/tess_shape_test.cc",
    repo_root=Path("/repo"),
    build_dir=Path("/repo/build/dev"),
    clang_tidy="clang-tidy",
  )

  assert check.command == (
    "clang-tidy",
    "--quiet",
    "-p",
    "/repo/build/dev",
    "/repo/tests/tess_shape_test.cc",
  )


def test_database_index_maps_absolute_file_paths():
  index = clang_tidy_changed.database_index(
    [
      {"directory": "/repo/build/dev", "file": "/repo/tests/a.cc"},
      {"directory": "/repo/build/dev", "file": "../../tests/b.cc"},
    ]
  )

  assert Path("/repo/tests/a.cc") in index
  assert Path("/repo/tests/b.cc") in index


def test_reference_entry_prefers_smoke_translation_unit():
  smoke = {"directory": "/repo/build/dev", "file": "/repo/tests/tess_smoke.cc"}
  other = {"directory": "/repo/build/dev", "file": "/repo/tests/a.cc"}

  assert (
    clang_tidy_changed.reference_entry([other, smoke], Path("/repo")) is smoke
  )


def test_reference_entry_requires_a_test_translation_unit():
  with pytest.raises(clang_tidy_changed.ToolError):
    clang_tidy_changed.reference_entry(
      [{"directory": "/b", "file": "/repo/examples/quickstart.cc"}],
      Path("/repo"),
    )


@pytest.mark.parametrize(
  "path",
  (
    ".clang-tidy",
    "CMakePresets.json",
    "CMakeLists.txt",
    "tests/CMakeLists.txt",
    "cmake/TessProjectOptions.cmake",
    "tools/clang_tidy_changed.py",
  ),
)
def test_configuration_changes_trigger_the_representative_check(path):
  assert clang_tidy_changed.is_config_trigger(path)


@pytest.mark.parametrize(
  "path",
  ("include/tess/tess.h", "tests/tess_shape_test.cc", "docs/index.md"),
)
def test_ordinary_changes_do_not_trigger_the_representative_check(path):
  assert not clang_tidy_changed.is_config_trigger(path)


def test_sources_in_the_database_are_checked():
  disposition = clang_tidy_changed.source_disposition(
    "tests/tess_shape_test.cc", in_database=True
  )

  assert disposition == "check"


@pytest.mark.parametrize(
  "path",
  (
    "examples/webgpu_compute/webgpu_compute.cc",
    "tests/fetchcontent_consumer/main.cc",
    "tests/install_consumer/main.cc",
    "tests/tess_grid_benchmark_data_test.cc",
  ),
)
def test_gated_sources_outside_the_database_are_skipped(path):
  assert (
    clang_tidy_changed.source_disposition(path, in_database=False) == "skip"
  )


def test_unwired_sources_fail_the_gate():
  disposition = clang_tidy_changed.source_disposition(
    "tests/tess_new_feature_test.cc", in_database=False
  )

  assert disposition == "fail"


def test_include_reference_forms():
  assert (
    clang_tidy_changed.include_reference("include/tess/ops/queued.h")
    == "tess/ops/queued.h"
  )
  assert (
    clang_tidy_changed.include_reference("tests/path_test_util.h")
    == "path_test_util.h"
  )


def test_find_includer_prefers_test_translation_units():
  tu_includes = {
    "examples/quickstart.cc": ("tess/ops/queued.h",),
    "tests/tess_queued_test.cc": ("tess/ops/queued.h", "gtest/gtest.h"),
  }

  includer = clang_tidy_changed.find_includer(
    "include/tess/ops/queued.h", tu_includes
  )

  assert includer == "tests/tess_queued_test.cc"


def test_find_includer_returns_none_without_direct_consumer():
  tu_includes = {"tests/tess_shape_test.cc": ("tess/core/shape.h",)}

  assert (
    clang_tidy_changed.find_includer("include/tess/ops/queued.h", tu_includes)
    is None
  )


def test_read_tu_includes_extracts_both_include_forms(tmp_path):
  source = tmp_path / "tests" / "sample.cc"
  source.parent.mkdir(parents=True)
  source.write_text(
    '#include "tess/ops/queued.h"\n'
    "#include <gtest/gtest.h>\n"
    "  #include  <tess/core/shape.h>\n"
    "// #include commentary\n",
    encoding="utf-8",
  )

  includes = clang_tidy_changed.read_tu_includes([source], tmp_path)

  assert includes == {
    "tests/sample.cc": (
      "tess/ops/queued.h",
      "gtest/gtest.h",
      "tess/core/shape.h",
    ),
  }
