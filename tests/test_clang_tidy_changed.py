"""Tests for the diff-scoped clang-tidy runner."""

from __future__ import annotations

import json
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
    ".github/workflows/ci.yml",
    "CMakePresets.json",
    "CMakeLists.txt",
    "tests/CMakeLists.txt",
    "cmake/TessProjectOptions.cmake",
    "include/tess/version.h.in",
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
    "examples/web_diagnostics/diagnostics_wasm.cc",
    "examples/webgpu_compute/webgpu_compute.cc",
    "tests/fetchcontent_consumer/main.cc",
    "tests/fuzz/tess_world_archive_fuzzer.cc",
    "tests/install_consumer/main.cc",
    "tests/no_exceptions_consumer_contract_main.cc",
    "tests/tess_grid_benchmark_data_test.cc",
    "tests/tess_no_exceptions_test.cc",
  ),
)
def test_gated_sources_outside_the_database_are_skipped(path):
  assert (
    clang_tidy_changed.source_disposition(path, in_database=False) == "skip"
  )


@pytest.mark.parametrize(
  "path",
  (
    "tests/tess_new_feature_test.cc",
    # A name sharing a gated file's prefix must not ride its exclusion.
    "tests/tess_grid_benchmark_data_unwired.cc",
  ),
)
def test_unwired_sources_fail_the_gate(path):
  assert (
    clang_tidy_changed.source_disposition(path, in_database=False) == "fail"
  )


def test_read_direct_includes_resolves_repo_files_only(tmp_path):
  (tmp_path / "include" / "tess" / "ops").mkdir(parents=True)
  (tmp_path / "include" / "tess" / "ops" / "queued.h").write_text(
    "#pragma once\n", encoding="utf-8"
  )
  source = tmp_path / "tests" / "sample.cc"
  source.parent.mkdir(parents=True)
  source.write_text(
    '#include "tess/ops/queued.h"\n'
    "#include <gtest/gtest.h>\n"
    "// #include commentary\n",
    encoding="utf-8",
  )

  includes = clang_tidy_changed.read_direct_includes([source], tmp_path)

  assert includes == {"tests/sample.cc": ("include/tess/ops/queued.h",)}


def test_include_closure_follows_transitive_edges_and_cycles():
  graph = {
    "tests/a.cc": ("include/tess/tess.h",),
    "include/tess/tess.h": ("include/tess/diagnostics/trace.h",),
    "include/tess/diagnostics/trace.h": ("include/tess/tess.h",),
  }

  closure = clang_tidy_changed.include_closure(("include/tess/tess.h",), graph)

  assert "include/tess/diagnostics/trace.h" in closure


@pytest.mark.parametrize(
  ("path", "macro"),
  (
    ("include/tess/diagnostics/trace.h", "TESS_ENABLE_DIAGNOSTICS"),
    ("include/tess/gpu/webgpu_backend.h", "TESS_ENABLE_WEBGPU"),
    ("include/tess/ecs/entt/entt_adapter.h", "TESS_ENABLE_ENTT"),
    ("include/tess/ecs/flecs/flecs_adapter.h", "TESS_ENABLE_FLECS"),
    ("include/tess/debug/imgui/panels.h", "TESS_ENABLE_IMGUI"),
    ("include/tess/ops/queued.h", None),
    ("tests/path_test_util.h", None),
  ),
)
def test_required_macro_maps_feature_gated_directories(path, macro):
  assert clang_tidy_changed.required_macro(path) == macro


def test_find_consumer_prefers_test_translation_units():
  tu_entries = {
    "examples/quickstart.cc": {"command": "clang++ -c quickstart.cc"},
    "tests/tess_queued_test.cc": {"command": "clang++ -c t.cc"},
  }
  graph = {
    "examples/quickstart.cc": ("include/tess/ops/queued.h",),
    "tests/tess_queued_test.cc": ("include/tess/ops/queued.h",),
  }

  consumer = clang_tidy_changed.find_consumer(
    "include/tess/ops/queued.h", tu_entries, graph
  )

  assert consumer == "tests/tess_queued_test.cc"


def test_find_consumer_reaches_transitively_included_headers():
  tu_entries = {"tests/tess_smoke.cc": {"command": "clang++ -c s.cc"}}
  graph = {
    "tests/tess_smoke.cc": ("include/tess/tess.h",),
    "include/tess/tess.h": ("include/tess/ops/queued.h",),
  }

  consumer = clang_tidy_changed.find_consumer(
    "include/tess/ops/queued.h", tu_entries, graph
  )

  assert consumer == "tests/tess_smoke.cc"


def test_find_consumer_requires_the_feature_macro_for_gated_headers():
  header = "include/tess/diagnostics/trace.h"
  graph = {
    "tests/tess_smoke.cc": (header,),
    "tests/tess_diagnostics_test.cc": (header,),
  }
  tu_entries = {
    "tests/tess_smoke.cc": {"command": "clang++ -c smoke.cc"},
    "tests/tess_diagnostics_test.cc": {
      "command": "clang++ -DTESS_ENABLE_DIAGNOSTICS -c d.cc"
    },
  }

  consumer = clang_tidy_changed.find_consumer(header, tu_entries, graph)

  assert consumer == "tests/tess_diagnostics_test.cc"


def test_find_consumer_returns_none_without_reaching_consumer():
  tu_entries = {"tests/tess_shape_test.cc": {"command": "clang++ -c s.cc"}}
  graph = {"tests/tess_shape_test.cc": ("include/tess/core/shape.h",)}

  assert (
    clang_tidy_changed.find_consumer(
      "include/tess/ops/queued.h", tu_entries, graph
    )
    is None
  )


def _end_to_end_repo(tmp_path):
  """Build a synthetic repo and database for orchestration tests."""
  repo = tmp_path / "repo"
  (repo / "include" / "tess").mkdir(parents=True)
  (repo / "tests").mkdir()
  build = repo / "build"
  build.mkdir()
  (repo / "include" / "tess" / "widget.h").write_text(
    "#pragma once\n", encoding="utf-8"
  )
  consumer = repo / "tests" / "tess_consumer_test.cc"
  consumer.write_text('#include "tess/widget.h"\n', encoding="utf-8")
  smoke = repo / "tests" / "tess_smoke.cc"
  smoke.write_text("int main() { return 0; }\n", encoding="utf-8")
  database = [
    {
      "directory": str(build),
      "command": f"clang++ -I{repo}/include -c {source}",
      "file": str(source),
    }
    for source in (consumer, smoke)
  ]
  (build / "compile_commands.json").write_text(
    json.dumps(database), encoding="utf-8"
  )
  return repo, build


def _run_main(repo, build, monkeypatch, changed):
  monkeypatch.setattr(
    clang_tidy_changed, "changed_paths", lambda _base, _head: changed
  )
  return clang_tidy_changed.main(
    (
      "a" * 40,
      "b" * 40,
      "--build-dir",
      str(build),
      "--repo-root",
      str(repo),
      "--clang-tidy",
      "true",  # every invocation exits 0; selection is what is under test
    )
  )


def test_main_checks_changed_headers_through_their_consumer(
  tmp_path, monkeypatch, capsys
):
  repo, build = _end_to_end_repo(tmp_path)

  result = _run_main(
    repo, build, monkeypatch, ("include/tess/widget.h",)
  )

  out = capsys.readouterr().out
  assert result == 0
  assert (
    "checking include/tess/widget.h through tests/tess_consumer_test.cc"
    in out
  )


def test_main_fails_on_an_unwired_changed_source(
  tmp_path, monkeypatch, capsys
):
  repo, build = _end_to_end_repo(tmp_path)
  (repo / "tests" / "tess_unwired_test.cc").write_text(
    "int main() { return 0; }\n", encoding="utf-8"
  )

  result = _run_main(
    repo, build, monkeypatch, ("tests/tess_unwired_test.cc",)
  )

  assert result == 1
  assert "no compilation-database entry" in capsys.readouterr().err


def test_main_adds_the_representative_check_for_config_changes(
  tmp_path, monkeypatch, capsys
):
  repo, build = _end_to_end_repo(tmp_path)

  result = _run_main(repo, build, monkeypatch, (".clang-tidy",))

  out = capsys.readouterr().out
  assert result == 0
  assert "configuration change: adding representative tests/tess_smoke.cc" in (
    out
  )


def test_main_skips_gated_sources_and_passes_with_no_candidates(
  tmp_path, monkeypatch, capsys
):
  repo, build = _end_to_end_repo(tmp_path)
  gated = repo / "tests" / "install_consumer"
  gated.mkdir()
  (gated / "main.cc").write_text("int main() {}\n", encoding="utf-8")

  result = _run_main(
    repo, build, monkeypatch, ("tests/install_consumer/main.cc",)
  )

  out = capsys.readouterr().out
  assert result == 0
  assert "skipping tests/install_consumer/main.cc" in out
  assert "no clang-tidy candidates changed" in out
