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
  include_root = root / "include" / "tess"
  for header in headers:
    target = include_root / header
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text("#pragma once\n", encoding="utf-8")
  return include_root


def _analyze(tmp_path, headers, exports, known_gaps=None):
  include_root = _tree(tmp_path, headers)
  export_paths = []
  for index, files in enumerate(exports):
    path = tmp_path / f"export-{index}.json"
    _export(path, files)
    export_paths.append(path)
  return coverage_gaps.analyze(
    export_paths, include_root, known_gaps=known_gaps or []
  )


def test_executed_header_is_not_a_gap(tmp_path):
  result = _analyze(
    tmp_path,
    ["path/astar.h"],
    [[("/ci/repo/include/tess/path/astar.h", 5, 9)]],
  )

  assert result["gaps"] == []
  row = result["headers"][0]
  assert row["header"] == "path/astar.h"
  assert row["executed"] is True


def test_zero_region_header_is_a_gap(tmp_path):
  result = _analyze(
    tmp_path,
    ["gpu/interface.h"],
    [[("/ci/repo/include/tess/gpu/interface.h", 0, 4)]],
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
    ["spatial/never_built.h"],
    [[("/ci/repo/include/tess/core/other.h", 1, 1)]],
  )

  gaps = {gap["header"]: gap for gap in result["gaps"]}
  assert gaps["spatial/never_built.h"]["reason"] == "absent-from-export"


def test_execution_in_any_export_counts(tmp_path):
  # The diagnostics binary may be the only executor of a header.
  result = _analyze(
    tmp_path,
    ["diagnostics/trace.h"],
    [
      [("/ci/repo/include/tess/diagnostics/trace.h", 0, 6)],
      [("/ci/repo/include/tess/diagnostics/trace.h", 4, 6)],
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
    [[("/ci/repo/include/tess/sim/movement.h", 0, 3)]],
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
    [[("/ci/repo/include/tess/gpu/backend.h", 2, 4)]],
    known_gaps=[{"header": "gpu/backend.h", "reason": "no benchmarks"}],
  )

  assert result["gaps"] == []
  assert result["known_gaps"] == []
  assert result["stale_known_gaps"] == ["gpu/backend.h"]


def test_files_outside_the_include_root_are_ignored(tmp_path):
  result = _analyze(
    tmp_path,
    ["core/grid.h"],
    [
      [
        ("/ci/repo/bench/tess_bench.cc", 9, 9),
        ("/ci/repo/build/_deps/benchmark-src/src/timers.cc", 3, 3),
        ("/ci/repo/include/tess/core/grid.h", 2, 2),
      ]
    ],
  )

  assert len(result["headers"]) == 1
  assert result["headers"][0]["header"] == "core/grid.h"


def test_subsystem_summary_counts_headers_and_gaps(tmp_path):
  result = _analyze(
    tmp_path,
    ["path/astar.h", "path/cache.h", "gpu/interface.h"],
    [
      [
        ("/ci/repo/include/tess/path/astar.h", 5, 9),
        ("/ci/repo/include/tess/path/cache.h", 0, 4),
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
    [[("/ci/repo/include/tess/tess.h", 1, 1)]],
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
    coverage_gaps.analyze([bad], include_root, known_gaps=[])


def test_corrupt_export_fails(tmp_path):
  include_root = _tree(tmp_path, ["core/grid.h"])
  bad = tmp_path / "export.json"
  bad.write_text("truncated{", encoding="utf-8")

  with pytest.raises(coverage_gaps.CoverageError):
    coverage_gaps.analyze([bad], include_root, known_gaps=[])


def test_missing_include_root_fails(tmp_path):
  export = tmp_path / "export.json"
  _export(export, [("/ci/repo/include/tess/core/grid.h", 1, 1)])

  with pytest.raises(coverage_gaps.CoverageError):
    coverage_gaps.analyze(
      [export], tmp_path / "missing", known_gaps=[]
    )


def test_report_lists_new_gaps_before_known_ones(tmp_path):
  result = _analyze(
    tmp_path,
    ["debug/imgui_panel.h", "sim/movement.h", "path/astar.h"],
    [[("/ci/repo/include/tess/path/astar.h", 3, 3)]],
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
    [[("/ci/repo/include/tess/path/astar.h", 3, 3)]],
  )

  report = coverage_gaps.render_report(result)

  assert "No new benchmark coverage gaps" in report


def test_main_writes_markdown_and_json(tmp_path):
  include_root = _tree(tmp_path, ["path/astar.h", "gpu/interface.h"])
  export = tmp_path / "export.json"
  _export(export, [("/ci/repo/include/tess/path/astar.h", 3, 3)])
  markdown = tmp_path / "report.md"
  payload = tmp_path / "report.json"

  code = coverage_gaps.main(
    [
      f"--export={export}",
      f"--include-root={include_root}",
      f"--out-markdown={markdown}",
      f"--out-json={payload}",
    ]
  )

  assert code == 0
  assert "gpu/interface.h" in markdown.read_text(encoding="utf-8")
  saved = json.loads(payload.read_text(encoding="utf-8"))
  assert [gap["header"] for gap in saved["gaps"]] == ["gpu/interface.h"]


def test_main_stays_zero_when_gaps_exist(tmp_path):
  # Advisory always: gaps are report content, not a failure.
  include_root = _tree(tmp_path, ["gpu/interface.h"])
  export = tmp_path / "export.json"
  _export(export, [("/ci/repo/include/tess/core/other.h", 1, 1)])

  code = coverage_gaps.main(
    [f"--export={export}", f"--include-root={include_root}"]
  )

  assert code == 0


def test_main_fails_loudly_on_missing_export(tmp_path):
  include_root = _tree(tmp_path, ["core/grid.h"])

  code = coverage_gaps.main(
    [
      f"--export={tmp_path / 'absent.json'}",
      f"--include-root={include_root}",
    ]
  )

  assert code == 1


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
      f"--known-gaps={gaps_file}",
      f"--out-json={payload}",
    ]
  )

  assert code == 0
  saved = json.loads(payload.read_text(encoding="utf-8"))
  assert saved["gaps"] == []
  assert [gap["header"] for gap in saved["known_gaps"]] == ["debug/panel.h"]
