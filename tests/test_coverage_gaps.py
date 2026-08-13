"""Tests for the benchmark coverage gap-finder."""

from __future__ import annotations

import json
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

import coverage_gaps  # noqa: E402


def _export(path, files):
  """Write an llvm-cov export JSON with the given file summaries."""
  entries = []
  for filename, covered, count in files:
    entries.append(
      {
        "filename": filename,
        "summary": {
          "regions": {"count": count, "covered": covered},
          "functions": {"count": max(count, 1), "covered": covered},
          "lines": {"count": count * 2, "covered": covered * 2},
        },
      }
    )
  path.write_text(
    json.dumps(
      {
        "type": "llvm.coverage.json.export",
        "version": "2.0.1",
        "data": [{"files": entries, "totals": {}}],
      }
    ),
    encoding="utf-8",
  )


def _tree(root, headers):
  """Header files plus a stability manifest declaring them public."""
  include_root = root / "include" / "tess"
  for header in headers:
    target = include_root / header
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text("#pragma once\n", encoding="utf-8")
  (root / "headers.json").write_text(json.dumps({
    "stable": [f"include/tess/{header}" for header in headers],
    "optional-stable": [],
    "experimental": [],
    "implementation-only": [],
  }), encoding="utf-8")
  return include_root


def _analyze(tmp_path, headers, exports, known_gaps=None):
  include_root = _tree(tmp_path, headers)
  export_paths = []
  for index, files in enumerate(exports):
    resolved = [
      (
        name if name.startswith("/") else str(include_root / name),
        covered,
        count,
      )
      for name, covered, count in files
    ]
    path = tmp_path / f"export-{index}.json"
    _export(path, resolved)
    export_paths.append(path)
  return coverage_gaps.analyze(
    export_paths, include_root, headers, known_gaps=known_gaps or []
  )


def test_executed_header_is_not_a_gap(tmp_path):
  result = _analyze(
    tmp_path,
    ["path/astar.h"],
    [[("path/astar.h", 5, 9)]],
  )

  assert result["gaps"] == []
  row = result["headers"][0]
  assert row["header"] == "path/astar.h"
  assert row["executed"] is True


def test_zero_region_header_is_a_gap(tmp_path):
  result = _analyze(
    tmp_path,
    ["gpu/interface.h"],
    [[("gpu/interface.h", 0, 4)]],
  )

  gaps = result["gaps"]
  assert [gap["header"] for gap in gaps] == ["gpu/interface.h"]
  assert gaps[0]["reason"] == "zero-covered-regions"


def test_header_absent_from_every_export_is_a_gap(tmp_path):
  # Never-included (or never-instantiated template) headers produce no
  # coverage mapping entry at all; absence must read as zero, not as
  # "not applicable".
  result = _analyze(
    tmp_path,
    ["spatial/never_built.h", "core/other.h"],
    [[("core/other.h", 1, 1)]],
  )

  gaps = {gap["header"]: gap for gap in result["gaps"]}
  assert gaps["spatial/never_built.h"]["reason"] == "absent-from-export"


def test_execution_in_any_export_counts(tmp_path):
  # The diagnostics binary may be the only executor of a header.
  result = _analyze(
    tmp_path,
    ["diagnostics/trace.h"],
    [
      [("diagnostics/trace.h", 0, 6)],
      [("diagnostics/trace.h", 4, 6)],
    ],
  )

  assert result["gaps"] == []
  assert result["headers"][0]["region_coverage"] == pytest.approx(4 / 6)


def test_known_gap_headers_are_reported_separately(tmp_path):
  # Exact headers, not directory prefixes: a NEW header under debug/
  # must surface as a new gap instead of inheriting the acknowledgment.
  result = _analyze(
    tmp_path,
    ["debug/imgui_panel.h", "debug/fresh.h", "sim/movement.h"],
    [[("sim/movement.h", 0, 3)]],
    known_gaps=[
      {"header": "debug/imgui_panel.h", "reason": "dev-only ImGui helpers"}
    ],
  )

  assert [gap["header"] for gap in result["gaps"]] == [
    "debug/fresh.h",
    "sim/movement.h",
  ]
  known = result["known_gaps"]
  assert [gap["header"] for gap in known] == ["debug/imgui_panel.h"]
  assert known[0]["known_reason"] == "dev-only ImGui helpers"


def test_known_gap_entry_for_an_executed_header_is_stale(tmp_path):
  # An acknowledged gap that gained benchmark coverage should be
  # flagged for manifest cleanup instead of silently ignored.
  result = _analyze(
    tmp_path,
    ["gpu/backend.h"],
    [[("gpu/backend.h", 2, 4)]],
    known_gaps=[{"header": "gpu/backend.h", "reason": "no benchmarks"}],
  )

  assert result["gaps"] == []
  assert result["known_gaps"] == []
  assert result["stale_known_gaps"] == ["gpu/backend.h"]


def test_orphan_known_gap_entry_is_stale(tmp_path):
  # A manifest entry naming a removed, renamed, or misspelled header
  # must surface for cleanup instead of silently vanishing.
  result = _analyze(
    tmp_path,
    ["path/astar.h"],
    [[("path/astar.h", 1, 1)]],
    known_gaps=[{"header": "gpu/removed.h", "reason": "stale entry"}],
  )

  assert result["stale_known_gaps"] == ["gpu/removed.h"]


def test_files_outside_the_include_root_are_ignored(tmp_path):
  result = _analyze(
    tmp_path,
    ["core/grid.h"],
    [
      [
        ("/ci/repo/bench/tess_bench.cc", 9, 9),
        ("/ci/repo/build/_deps/benchmark-src/src/timers.cc", 3, 3),
        ("core/grid.h", 2, 2),
      ]
    ],
  )

  assert len(result["headers"]) == 1
  assert result["headers"][0]["header"] == "core/grid.h"


def test_lookalike_dependency_paths_do_not_count(tmp_path):
  # A dependency path that merely CONTAINS include/tess/ must not mark
  # the repository header as executed; matching is by resolved prefix.
  result = _analyze(
    tmp_path,
    ["core/grid.h"],
    [[("/deps/vendored/include/tess/core/grid.h", 5, 5)]],
  )

  assert [gap["header"] for gap in result["gaps"]] == ["core/grid.h"]


def test_subsystem_summary_counts_headers_and_gaps(tmp_path):
  result = _analyze(
    tmp_path,
    ["path/astar.h", "path/cache.h", "gpu/interface.h"],
    [
      [
        ("path/astar.h", 5, 9),
        ("path/cache.h", 0, 4),
      ]
    ],
  )

  subsystems = {row["subsystem"]: row for row in result["subsystems"]}
  assert subsystems["path"]["headers"] == 2
  assert subsystems["path"]["gaps"] == 1
  assert subsystems["gpu"]["headers"] == 1
  assert subsystems["gpu"]["gaps"] == 1


def test_top_level_headers_form_their_own_subsystem(tmp_path):
  result = _analyze(
    tmp_path,
    ["tess.h"],
    [[("tess.h", 1, 1)]],
  )

  assert result["subsystems"][0]["subsystem"] == "(top-level)"


def test_unrecognized_export_type_fails(tmp_path):
  include_root = _tree(tmp_path, ["core/grid.h"])
  bad = tmp_path / "export.json"
  bad.write_text(
    json.dumps({"type": "something-else", "version": "9", "data": []}),
    encoding="utf-8",
  )

  with pytest.raises(coverage_gaps.CoverageError):
    coverage_gaps.analyze(
      [bad], include_root, ["core/grid.h"], known_gaps=[]
    )


def test_corrupt_export_fails(tmp_path):
  include_root = _tree(tmp_path, ["core/grid.h"])
  bad = tmp_path / "export.json"
  bad.write_text("truncated{", encoding="utf-8")

  with pytest.raises(coverage_gaps.CoverageError):
    coverage_gaps.analyze(
      [bad], include_root, ["core/grid.h"], known_gaps=[]
    )


def test_export_with_non_list_data_fails(tmp_path):
  # Schema violations must surface as CoverageError, not TypeError.
  include_root = _tree(tmp_path, ["core/grid.h"])
  bad = tmp_path / "export.json"
  bad.write_text(
    json.dumps(
      {
        "type": "llvm.coverage.json.export",
        "version": "2.0.1",
        "data": {"files": []},
      }
    ),
    encoding="utf-8",
  )

  with pytest.raises(coverage_gaps.CoverageError):
    coverage_gaps.analyze(
      [bad], include_root, ["core/grid.h"], known_gaps=[]
    )


def test_missing_include_root_fails(tmp_path):
  export = tmp_path / "export.json"
  _export(export, [("/ci/include/tess/core/grid.h", 1, 1)])

  with pytest.raises(coverage_gaps.CoverageError):
    coverage_gaps.analyze(
      [export], tmp_path / "missing", ["core/grid.h"], known_gaps=[]
    )


def test_public_headers_come_from_the_declared_set(tmp_path):
  # Experimental and implementation headers are outside compatibility
  # coverage, while generated version.h is included.
  manifest = tmp_path / "headers.json"
  manifest.write_text(json.dumps({
    "stable": ["include/tess/core/lattice.h"],
    "optional-stable": ["include/tess/path/astar.h"],
    "experimental": ["include/tess/experimental/tool.h"],
    "implementation-only": ["include/tess/path/detail/astar.h"],
  }), encoding="utf-8")

  headers = coverage_gaps.public_headers(manifest)

  assert headers == ["core/lattice.h", "path/astar.h", "version.h"]


def test_public_headers_without_stable_classes_fails(tmp_path):
  manifest = tmp_path / "headers.json"
  manifest.write_text("{}\n", encoding="utf-8")

  with pytest.raises(coverage_gaps.CoverageError):
    coverage_gaps.public_headers(manifest)


def test_report_lists_new_gaps_before_known_ones(tmp_path):
  result = _analyze(
    tmp_path,
    ["debug/imgui_panel.h", "sim/movement.h", "path/astar.h"],
    [[("path/astar.h", 3, 3)]],
    known_gaps=[
      {"header": "debug/imgui_panel.h", "reason": "dev-only ImGui helpers"}
    ],
  )

  report = coverage_gaps.render_report(result)

  assert report.index("sim/movement.h") < report.index("debug/imgui_panel.h")
  assert "New benchmark coverage gaps" in report
  assert "advisory" in report.lower()


def test_report_with_no_new_gaps_says_so(tmp_path):
  result = _analyze(
    tmp_path,
    ["path/astar.h"],
    [[("path/astar.h", 3, 3)]],
  )

  report = coverage_gaps.render_report(result)

  assert "No new benchmark coverage gaps" in report


def _ctest_json(tmp_path, commands):
  payload = tmp_path / "ctest.json"
  payload.write_text(
    json.dumps(
      {
        "kind": "ctest",
        "tests": [{"name": f"t{i}", "command": command}
                  for i, command in enumerate(commands)],
      }
    ),
    encoding="utf-8",
  )
  return payload


def test_ctest_objects_deduplicates_build_dir_executables(tmp_path):
  build = tmp_path / "build"
  (build / "tests").mkdir(parents=True)
  binary_a = build / "tests" / "tess_core_test"
  binary_b = build / "tests" / "tess_path_test"
  for binary in (binary_a, binary_b):
    binary.write_bytes(b"\x7fELF")
  payload = _ctest_json(
    tmp_path,
    [
      [str(binary_a), "--gtest_filter=A.*"],
      [str(binary_a), "--gtest_filter=B.*"],
      [str(binary_b)],
    ],
  )

  objects = coverage_gaps.ctest_objects(payload, build)

  assert objects == sorted([str(binary_a), str(binary_b)])


def test_ctest_objects_resolves_gtest_launcher_commands(tmp_path):
  # gtest_discover_tests registers tests as cmake launcher commands;
  # command[0] is cmake, and the instrumented binary hides in a
  # TEST_EXECUTABLE argument (both -D forms occur).
  build = tmp_path / "build"
  (build / "tests").mkdir(parents=True)
  binary_a = build / "tests" / "tess_core_test"
  binary_b = build / "tests" / "tess_path_test"
  for binary in (binary_a, binary_b):
    binary.write_bytes(b"\x7fELF")
  payload = _ctest_json(
    tmp_path,
    [
      [
        "/usr/bin/cmake",
        "-D",
        f"TEST_EXECUTABLE={binary_a}",
        "-D",
        "TEST_EXECUTOR=",
        "-P",
        "/usr/share/cmake/GoogleTest/GoogleTestAddTests.cmake",
      ],
      [
        "/usr/bin/cmake",
        f"-DTEST_EXECUTABLE={binary_b}",
        "-P",
        "/usr/share/cmake/GoogleTest/GoogleTestAddTests.cmake",
      ],
    ],
  )

  objects = coverage_gaps.ctest_objects(payload, build)

  assert objects == sorted([str(binary_a), str(binary_b)])


def test_ctest_objects_skips_interpreters_and_outside_paths(tmp_path):
  build = tmp_path / "build"
  (build / "tests").mkdir(parents=True)
  probe = build / "tests" / "tess_counter_golden_probe"
  probe.write_bytes(b"\x7fELF")
  payload = _ctest_json(
    tmp_path,
    [
      [str(probe)],
      ["/usr/bin/python3", "tools/check_counter_goldens.py"],
      ["/usr/bin/cmake", "-E", "true"],
    ],
  )

  objects = coverage_gaps.ctest_objects(payload, build)

  assert objects == [str(probe)]


def test_ctest_objects_with_no_binaries_fails(tmp_path):
  build = tmp_path / "build"
  build.mkdir()
  payload = _ctest_json(tmp_path, [["/usr/bin/python3", "x.py"]])

  with pytest.raises(coverage_gaps.CoverageError):
    coverage_gaps.ctest_objects(payload, build)


def test_objects_main_prints_one_path_per_line(tmp_path, capsys):
  build = tmp_path / "build"
  (build / "tests").mkdir(parents=True)
  binary = build / "tests" / "tess_core_test"
  binary.write_bytes(b"\x7fELF")
  payload = _ctest_json(tmp_path, [[str(binary)]])

  code = coverage_gaps.objects_main(
    [f"--ctest-json={payload}", f"--build-dir={build}"]
  )

  assert code == 0
  assert capsys.readouterr().out == f"{binary}\n"


def test_objects_main_fails_without_binaries(tmp_path, capsys):
  build = tmp_path / "build"
  build.mkdir()
  payload = _ctest_json(tmp_path, [["/usr/bin/python3", "x.py"]])

  code = coverage_gaps.objects_main(
    [f"--ctest-json={payload}", f"--build-dir={build}"]
  )

  assert code == 1
  assert "no instrumented test executables" in capsys.readouterr().err


def test_main_writes_markdown_and_json(tmp_path):
  include_root = _tree(tmp_path, ["path/astar.h", "gpu/interface.h"])
  export = tmp_path / "export.json"
  _export(export, [(str(include_root / "path/astar.h"), 3, 3)])
  markdown = tmp_path / "report.md"
  payload = tmp_path / "report.json"

  code = coverage_gaps.main(
    [
      f"--export={export}",
      f"--include-root={include_root}",
      f"--headers-manifest={tmp_path / 'headers.json'}",
      f"--out-markdown={markdown}",
      f"--out-json={payload}",
    ]
  )

  assert code == 0
  assert "gpu/interface.h" in markdown.read_text(encoding="utf-8")
  saved = json.loads(payload.read_text(encoding="utf-8"))
  gap_headers = [gap["header"] for gap in saved["gaps"]]
  assert "gpu/interface.h" in gap_headers
  # The generated public header is part of the inventory even though
  # it never exists in the source tree.
  assert "version.h" in gap_headers


def test_main_stays_zero_when_gaps_exist(tmp_path):
  # Advisory always: gaps are report content, not a failure.
  include_root = _tree(tmp_path, ["gpu/interface.h"])
  export = tmp_path / "export.json"
  _export(export, [])

  code = coverage_gaps.main(
    [
      f"--export={export}",
      f"--include-root={include_root}",
      f"--headers-manifest={tmp_path / 'headers.json'}",
    ]
  )

  assert code == 0


def test_main_fails_loudly_on_missing_export(tmp_path):
  include_root = _tree(tmp_path, ["core/grid.h"])

  code = coverage_gaps.main(
    [
      f"--export={tmp_path / 'absent.json'}",
      f"--include-root={include_root}",
      f"--headers-manifest={tmp_path / 'headers.json'}",
    ]
  )

  assert code == 1


def test_main_fails_loudly_on_unwritable_output(tmp_path):
  include_root = _tree(tmp_path, ["core/grid.h"])
  export = tmp_path / "export.json"
  _export(export, [])

  code = coverage_gaps.main(
    [
      f"--export={export}",
      f"--include-root={include_root}",
      f"--headers-manifest={tmp_path / 'headers.json'}",
      f"--out-markdown={tmp_path}",
    ]
  )

  assert code == 1


def test_duplicate_known_gap_entries_fail(tmp_path):
  include_root = _tree(tmp_path, ["core/grid.h"])
  export = tmp_path / "export.json"
  _export(export, [])
  gaps_file = tmp_path / "known.json"
  gaps_file.write_text(
    json.dumps(
      {
        "known_gaps": [
          {"header": "core/grid.h", "reason": "one"},
          {"header": "core/grid.h", "reason": "two"},
        ]
      }
    ),
    encoding="utf-8",
  )

  code = coverage_gaps.main(
    [
      f"--export={export}",
      f"--include-root={include_root}",
      f"--headers-manifest={tmp_path / 'headers.json'}",
      f"--known-gaps={gaps_file}",
    ]
  )

  assert code == 1


def test_known_gaps_file_round_trips(tmp_path):
  include_root = _tree(tmp_path, ["debug/panel.h"])
  export = tmp_path / "export.json"
  _export(export, [])
  gaps_file = tmp_path / "known.json"
  gaps_file.write_text(
    json.dumps(
      {
        "known_gaps": [
          {"header": "debug/panel.h", "reason": "dev-only ImGui helpers"}
        ]
      }
    ),
    encoding="utf-8",
  )
  payload = tmp_path / "report.json"

  code = coverage_gaps.main(
    [
      f"--export={export}",
      f"--include-root={include_root}",
      f"--headers-manifest={tmp_path / 'headers.json'}",
      f"--known-gaps={gaps_file}",
      f"--out-json={payload}",
    ]
  )

  assert code == 0
  saved = json.loads(payload.read_text(encoding="utf-8"))
  assert saved["gaps"] != []  # version.h is a new gap in this fixture
  assert [gap["header"] for gap in saved["known_gaps"]] == ["debug/panel.h"]
