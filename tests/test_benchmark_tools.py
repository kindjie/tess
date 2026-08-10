"""Unit tests for the benchmark threshold, summary, and trend tools."""

from __future__ import annotations

import csv
import io
import json
import re
import sys
from datetime import datetime
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

import benchmark_artifact_metadata  # noqa: E402
import benchmark_baseline_summary  # noqa: E402
import benchmark_thresholds  # noqa: E402
import benchmark_trends  # noqa: E402


def literal_benchmark_names(source: str) -> set[str]:
  """Extract adjacent string literals passed directly to Benchmark::Name."""
  calls = re.findall(
      r'->Name\(\s*((?:"[^"]*"\s*)+)\)',
      source,
      flags=re.DOTALL,
  )
  return {
      "".join(re.findall(r'"([^"]*)"', literal_sequence))
      for literal_sequence in calls
  }


def entry(
    name: str,
    cpu_time: float,
    *,
    time_unit: str = "ns",
    run_type: str = "iteration",
    aggregate_name: str | None = None,
    run_name: str | None = None,
    real_time: float | None = None,
) -> dict:
  benchmark = {
      "name": name,
      "run_name": run_name if run_name is not None else name,
      "run_type": run_type,
      "real_time": cpu_time if real_time is None else real_time,
      "cpu_time": cpu_time,
      "time_unit": time_unit,
  }
  if aggregate_name is not None:
    benchmark["aggregate_name"] = aggregate_name
  return benchmark


def write_json(path: Path, data: dict) -> Path:
  path.write_text(json.dumps(data), encoding="utf-8")
  return path


def run_thresholds(
    tmp_path: Path,
    benchmarks: list[dict],
    thresholds: dict,
    extra_args: tuple[str, ...] = (),
) -> int:
  results_path = write_json(
      tmp_path / "results.json", {"benchmarks": benchmarks}
  )
  thresholds_path = write_json(tmp_path / "thresholds.json", thresholds)
  return benchmark_thresholds.main(
      [
          "--results",
          str(results_path),
          "--thresholds",
          str(thresholds_path),
          *extra_args,
      ]
  )


def limits(max_cpu_time_ns: float | None) -> dict:
  return {"max_real_time_ns": None, "max_cpu_time_ns": max_cpu_time_ns}


def test_duplicate_benchmark_names_are_rejected(tmp_path, capsys):
  benchmarks = [entry("key/dup", 100.0), entry("key/dup", 900.0)]
  thresholds = {"benchmarks": {"key/dup": limits(500.0)}}

  code = run_thresholds(tmp_path, benchmarks, thresholds)

  assert code == 1
  assert "duplicate" in capsys.readouterr().err


def test_repetition_aggregates_prefer_median(tmp_path):
  benchmarks = [
      entry("key/rep", 100.0, run_name="key/rep"),
      entry("key/rep", 200.0, run_name="key/rep"),
      entry("key/rep", 900.0, run_name="key/rep"),
      entry(
          "key/rep_mean",
          400.0,
          run_type="aggregate",
          aggregate_name="mean",
          run_name="key/rep",
      ),
      entry(
          "key/rep_median",
          200.0,
          run_type="aggregate",
          aggregate_name="median",
          run_name="key/rep",
      ),
      entry(
          "key/rep_stddev",
          350.0,
          run_type="aggregate",
          aggregate_name="stddev",
          run_name="key/rep",
      ),
  ]
  thresholds = {"benchmarks": {"key/rep": limits(250.0)}}

  assert run_thresholds(tmp_path, benchmarks, thresholds) == 0


def test_aggregate_flag_selects_other_aggregate(tmp_path, capsys):
  benchmarks = [
      entry(
          "key/rep_mean",
          400.0,
          run_type="aggregate",
          aggregate_name="mean",
          run_name="key/rep",
      ),
      entry(
          "key/rep_median",
          200.0,
          run_type="aggregate",
          aggregate_name="median",
          run_name="key/rep",
      ),
  ]
  thresholds = {"benchmarks": {"key/rep": limits(250.0)}}

  code = run_thresholds(
      tmp_path, benchmarks, thresholds, extra_args=("--aggregate", "mean")
  )

  assert code == 1
  assert "exceeds" in capsys.readouterr().err


def test_missing_requested_aggregate_is_an_error(tmp_path, capsys):
  benchmarks = [
      entry(
          "key/rep_mean",
          400.0,
          run_type="aggregate",
          aggregate_name="mean",
          run_name="key/rep",
      ),
  ]
  thresholds = {"benchmarks": {"key/rep": limits(500.0)}}

  code = run_thresholds(
      tmp_path, benchmarks, thresholds, extra_args=("--aggregate", "median")
  )

  assert code == 1
  assert "median" in capsys.readouterr().err


def test_all_time_units_convert_to_nanoseconds(tmp_path):
  benchmarks = [
      entry("unit/ns", 900.0, time_unit="ns"),
      entry("unit/us", 0.9, time_unit="us"),
      entry("unit/ms", 0.0009, time_unit="ms"),
      entry("unit/s", 0.0000009, time_unit="s"),
  ]
  thresholds = {
      "benchmarks": {name: limits(1000.0) for name in
                     ("unit/ns", "unit/us", "unit/ms", "unit/s")}
  }

  assert run_thresholds(tmp_path, benchmarks, thresholds) == 0


def test_time_unit_conversion_detects_regression(tmp_path, capsys):
  benchmarks = [entry("unit/us", 2.0, time_unit="us")]
  thresholds = {"benchmarks": {"unit/us": limits(1500.0)}}

  code = run_thresholds(tmp_path, benchmarks, thresholds)

  assert code == 1
  assert "2000.000 ns" in capsys.readouterr().err


def test_missing_benchmark_fails(tmp_path, capsys):
  benchmarks = [entry("key/present", 100.0)]
  thresholds = {"benchmarks": {"key/absent": limits(500.0)}}

  code = run_thresholds(tmp_path, benchmarks, thresholds)

  assert code == 1
  assert "missing benchmark result" in capsys.readouterr().err


def test_explicit_feature_disabled_benchmark_may_be_missing(tmp_path):
  benchmarks = [entry("ecs/tick_entt", 100.0)]
  thresholds = {
      "benchmarks": {
          "ecs/tick_entt": limits(500.0),
          "ecs/flecs_collect": limits(500.0),
      }
  }

  assert run_thresholds(
      tmp_path,
      benchmarks,
      thresholds,
      extra_args=("--allow-missing-result", "ecs/flecs_collect"),
  ) == 0


def test_allow_missing_result_rejects_unknown_threshold(tmp_path, capsys):
  benchmarks = [entry("ecs/tick_entt", 100.0)]
  thresholds = {"benchmarks": {"ecs/tick_entt": limits(500.0)}}

  code = run_thresholds(
      tmp_path,
      benchmarks,
      thresholds,
      extra_args=("--allow-missing-result", "ecs/not_declared"),
  )

  assert code == 1
  assert "unknown allowed-missing threshold" in capsys.readouterr().err


def test_unthresholded_benchmark_fails(tmp_path, capsys):
  benchmarks = [
      entry("key/covered", 100.0),
      entry("key/unthresholded", 100.0),
  ]
  thresholds = {"benchmarks": {"key/covered": limits(500.0)}}

  code = run_thresholds(tmp_path, benchmarks, thresholds)

  assert code == 1
  assert "key/unthresholded: missing threshold entry" in (
      capsys.readouterr().err
  )


def test_empty_benchmark_results_fail(tmp_path, capsys):
  code = run_thresholds(tmp_path, [], {"benchmarks": {}})

  assert code == 1
  assert "no benchmark results" in capsys.readouterr().err


def test_threshold_entry_without_a_limit_fails(tmp_path, capsys):
  benchmarks = [entry("key/free", 1e12)]
  thresholds = {"benchmarks": {"key/free": limits(None)}}

  assert run_thresholds(tmp_path, benchmarks, thresholds) == 1
  assert "no enabled time limit" in capsys.readouterr().err


def test_explicitly_ungated_threshold_entry_is_allowed(tmp_path):
  benchmarks = [entry("key/free", 1e12)]
  entry_limits = limits(None)
  entry_limits["gating"] = False
  thresholds = {"benchmarks": {"key/free": entry_limits}}

  assert run_thresholds(tmp_path, benchmarks, thresholds) == 0


def test_ungated_entry_rejects_a_limit(tmp_path, capsys):
  benchmarks = [entry("key/conflict", 100.0)]
  entry_limits = limits(500.0)
  entry_limits["gating"] = False

  code = run_thresholds(
      tmp_path, benchmarks, {"benchmarks": {"key/conflict": entry_limits}}
  )

  assert code == 1
  assert "gating=false conflicts" in capsys.readouterr().err


def test_bad_time_unit_is_a_normal_tool_error(tmp_path, capsys):
  benchmarks = [entry("key/unit", 100.0, time_unit="ticks")]
  thresholds = {"benchmarks": {"key/unit": limits(500.0)}}

  code = run_thresholds(tmp_path, benchmarks, thresholds)

  assert code == 1
  assert "key/unit: unsupported time_unit 'ticks'" in capsys.readouterr().err


def test_unknown_limit_key_is_rejected(tmp_path, capsys):
  benchmarks = [entry("key/typo", 100.0)]
  entry_limits = limits(500.0)
  entry_limits["max_cpu_tim_ns"] = 1.0

  code = run_thresholds(
      tmp_path, benchmarks, {"benchmarks": {"key/typo": entry_limits}}
  )

  assert code == 1
  err = capsys.readouterr().err
  assert "max_cpu_tim_ns" in err
  assert "unknown" in err


def test_annotation_keys_are_allowed(tmp_path):
  benchmarks = [entry("key/annotated", 100.0)]
  entry_limits = limits(500.0)
  entry_limits.update(
      {
          "comment": "why the ceiling is what it is",
          "comment_ref": "docs/planning/optimization-log.md",
          "note": "extra provenance",
      }
  )

  code = run_thresholds(
      tmp_path, benchmarks, {"benchmarks": {"key/annotated": entry_limits}}
  )

  assert code == 0


def test_repo_threshold_files_use_only_allowed_keys():
  thresholds_dir = Path(__file__).resolve().parents[1] / "bench" / "thresholds"
  for path in sorted(thresholds_dir.glob("*.json")):
    data = json.loads(path.read_text(encoding="utf-8"))
    for name, entry_limits in data.get("benchmarks", {}).items():
      unknown = set(entry_limits) - benchmark_thresholds.ALLOWED_LIMIT_KEYS
      assert not unknown, f"{path.name}: {name}: {sorted(unknown)}"


def test_literal_gated_benchmarks_have_threshold_entries():
  root = Path(__file__).resolve().parents[1]
  benchmark_names = set()
  for path in sorted((root / "bench").glob("*.cc")):
    source = path.read_text(encoding="utf-8")
    benchmark_names.update(literal_benchmark_names(source))
  threshold_files = {
    "key": "key-conversions.json",
    "storage": "storage.json",
    "block": "block.json",
    "block_pipeline": "block-pipeline.json",
    "queued": "queued.json",
    "path": "path.json",
    "topology": "topology.json",
    "scheduler": "scheduler.json",
    "residency": "residency.json",
    "maintenance": "maintenance.json",
    "persistence": "persistence.json",
    "query": "query.json",
    "spatial": "spatial.json",
    "parallel": "parallel.json",
    "ecs": "ecs.json",
    "render_delta": "render-delta.json",
    "fields": "fields.json",
    "diagnostics": "diagnostics.json",
  }
  thresholds = {}
  for prefix, file_name in threshold_files.items():
    data = json.loads(
      (root / "bench" / "thresholds" / file_name).read_text(
        encoding="utf-8"
      )
    )
    thresholds[prefix] = data["benchmarks"]
  uncovered = {
    name
    for name in benchmark_names
    if (prefix := name.partition("/")[0]) in thresholds
    and name not in thresholds[prefix]
    and not any(
      key.startswith(f"{name}/") for key in thresholds[prefix]
    )
  }

  assert not uncovered, sorted(uncovered)


def test_every_threshold_manifest_has_a_gate_target():
  """No manifest may exist without a target that runs it.

  Previously this checked five hardcoded families, so a sixth could ship
  a manifest and a target and be gated nowhere. The manifests on disk are
  the authority now.
  """
  root = Path(__file__).resolve().parents[1]
  cmake = (root / "bench" / "CMakeLists.txt").read_text(encoding="utf-8")
  # bench/thresholds/<stem>.json <-> tess_bench_<family>_thresholds, with
  # hyphenated stems mapping to underscored target names.
  manifests = sorted(
    path.stem for path in (root / "bench" / "thresholds").glob("*.json")
  )

  assert manifests, "no threshold manifests found; the check is vacuous"
  missing = []
  for stem in manifests:
    family = "key" if stem == "key-conversions" else stem.replace("-", "_")
    if f"tess_bench_{family}_thresholds" not in cmake:
      missing.append(stem)

  assert missing == []


def test_ci_gates_thresholds_through_the_derived_aggregate():
  """CI must not re-list the families it gates.

  A hand-maintained loop in the workflow let a new family ship a
  manifest and a target and still never gate, with every test green.
  The aggregate target derives the set from the targets CMake defines.
  """
  root = Path(__file__).resolve().parents[1]
  cmake = (root / "bench" / "CMakeLists.txt").read_text(encoding="utf-8")
  workflow = (root / ".github" / "workflows" / "ci.yml").read_text(
    encoding="utf-8"
  )

  assert "BUILDSYSTEM_TARGETS" in cmake
  assert "add_custom_target(\n    tess_bench_all_thresholds" in cmake

  threshold_step = workflow.split("      - name: Benchmark thresholds\n", 1)
  assert len(threshold_step) == 2
  step = threshold_step[1].split("      - name:", 1)[0]

  assert "--target tess_bench_all_thresholds" in step
  # Timing gates must not run concurrently with one another.
  assert "--parallel 1" in step
  # The old shape: a `for family in ...` loop enumerating the set here.
  assert "for family in" not in step


def test_literal_benchmark_names_accept_multiline_adjacent_literals():
  source = '''
BENCHMARK(sample)->Name(
    "path/weighted_"
    "portal_segments");
'''

  assert literal_benchmark_names(source) == {
      "path/weighted_portal_segments"
  }


def test_malformed_results_file_reports_clear_error(tmp_path, capsys):
  results_path = tmp_path / "results.json"
  results_path.write_text("{not json", encoding="utf-8")
  thresholds_path = write_json(
      tmp_path / "thresholds.json", {"benchmarks": {}}
  )

  code = benchmark_thresholds.main(
      ["--results", str(results_path), "--thresholds", str(thresholds_path)]
  )

  assert code == 1
  err = capsys.readouterr().err
  assert "results.json" in err
  assert "JSON" in err


def test_missing_results_file_reports_clear_error(tmp_path, capsys):
  thresholds_path = write_json(
      tmp_path / "thresholds.json", {"benchmarks": {}}
  )

  code = benchmark_thresholds.main(
      [
          "--results",
          str(tmp_path / "does-not-exist.json"),
          "--thresholds",
          str(thresholds_path),
      ]
  )

  assert code == 1
  assert "does-not-exist.json" in capsys.readouterr().err


def run_summary(tmp_path, benchmarks: list[dict]) -> tuple[int, str, str]:
  results_path = write_json(
      tmp_path / "baseline.json", {"benchmarks": benchmarks}
  )
  stdout = io.StringIO()
  stderr = io.StringIO()
  old_out, old_err = sys.stdout, sys.stderr
  sys.stdout, sys.stderr = stdout, stderr
  try:
    code = benchmark_baseline_summary.main([str(results_path)])
  finally:
    sys.stdout, sys.stderr = old_out, old_err
  return code, stdout.getvalue(), stderr.getvalue()


def test_summary_filters_aggregates_by_run_type(tmp_path):
  benchmarks = [
      entry("key/base", 100.0),
      entry("key/base", 200.0),
      entry(
          "key/base_max",
          200.0,
          run_type="aggregate",
          aggregate_name="max",
          run_name="key/base",
      ),
  ]

  code, out, _ = run_summary(tmp_path, benchmarks)

  assert code == 0
  rows = list(csv.reader(io.StringIO(out)))
  names = [row[0] for row in rows[1:]]
  assert names == ["key/base"]
  assert rows[1][1] == "2"


def test_summary_quotes_names_containing_commas(tmp_path):
  benchmarks = [entry("weird,name/bench", 100.0)]

  code, out, _ = run_summary(tmp_path, benchmarks)

  assert code == 0
  rows = list(csv.reader(io.StringIO(out)))
  assert rows[1][0] == "weird,name/bench"
  assert len(rows[1]) == len(rows[0])


def test_summary_malformed_file_reports_clear_error(tmp_path):
  results_path = tmp_path / "broken.json"
  results_path.write_text("[1, 2", encoding="utf-8")

  stdout = io.StringIO()
  stderr = io.StringIO()
  old_out, old_err = sys.stdout, sys.stderr
  sys.stdout, sys.stderr = stdout, stderr
  try:
    code = benchmark_baseline_summary.main([str(results_path)])
  finally:
    sys.stdout, sys.stderr = old_out, old_err

  assert code == 1
  assert "broken.json" in stderr.getvalue()


BASELINE_FILE_NAMES = (
    "block.json",
    "block-pipeline.json",
    "storage.json",
    "key.json",
    "queued.json",
    "path.json",
    "topology.json",
    "parallel.json",
    "scheduler.json",
    "residency.json",
    "maintenance.json",
    "persistence.json",
    "query.json",
    "spatial.json",
    "diagnostics.json",
    "ecs.json",
    "render-delta.json",
    "fields.json",
)


def write_artifact(tmp_path: Path, name: str = "artifact") -> Path:
  """Build a fake CI baseline artifact with every benchmark result file."""
  directory = tmp_path / name
  directory.mkdir()
  for file_name in BASELINE_FILE_NAMES:
    family = file_name.removesuffix(".json")
    write_json(
        directory / file_name,
        {"benchmarks": [entry(f"{family}/sample", 100.0)]},
    )
  write_json(directory / "metadata.json", {"commit": "a" * 40})
  return directory


def test_trends_collects_values_from_every_baseline_file(tmp_path):
  directory = write_artifact(tmp_path)
  benchmarks = tuple(
      f"{name.removesuffix('.json')}/sample" for name in BASELINE_FILE_NAMES
  )

  values = benchmark_trends.collect_values(directory, benchmarks)

  assert set(values) == set(benchmarks)
  assert all(value == 100.0 for value in values.values())


def test_trends_skips_metadata_and_aggregate_entries(tmp_path):
  directory = tmp_path / "artifact"
  directory.mkdir()
  write_json(
      directory / "path.json",
      {
          "benchmarks": [
              entry("path/sample", 100.0),
              entry("path/sample_median", 999.0),
          ]
      },
  )
  write_json(directory / "metadata.json", {"path/sample": "not a result"})

  values = benchmark_trends.collect_values(directory, ("path/sample",))

  assert values == {"path/sample": 100.0}


def test_trends_ignores_non_object_json_files(tmp_path):
  directory = write_artifact(tmp_path)
  (directory / "stray.json").write_text("[1, 2, 3]", encoding="utf-8")

  values = benchmark_trends.collect_values(directory, ("path/sample",))

  assert values == {"path/sample": 100.0}


def test_trends_unmatched_selector_is_an_error(tmp_path, capsys):
  directory = write_artifact(tmp_path)

  code = benchmark_trends.main(
      [str(directory), "--benchmark", "path/does_not_exist"]
  )

  assert code == 1
  err = capsys.readouterr().err
  assert "path/does_not_exist" in err


def test_trends_matched_selector_renders_value(tmp_path, capsys):
  directory = write_artifact(tmp_path)

  code = benchmark_trends.main(
      [str(directory), "--benchmark", "topology/sample"]
  )

  assert code == 0
  out = capsys.readouterr().out
  assert "topology/sample" in out
  assert "100.000" in out
  assert "n/a" not in out


def test_trends_default_benchmarks_stay_lenient(tmp_path, capsys):
  directory = write_artifact(tmp_path)

  code = benchmark_trends.main([str(directory)])

  assert code == 0
  assert "n/a" in capsys.readouterr().out


def test_metadata_writes_environment_fields(tmp_path, monkeypatch):
  values = {
      "GITHUB_SHA": "b" * 40,
      "GITHUB_REF_NAME": "main",
      "GITHUB_RUN_ID": "123",
      "GITHUB_RUN_NUMBER": "7",
      "GITHUB_WORKFLOW": "CI",
      "RUNNER_OS": "Linux",
  }
  for name, value in values.items():
    monkeypatch.setenv(name, value)
  out = tmp_path / "nested" / "metadata.json"

  code = benchmark_artifact_metadata.main(["--out", str(out)])

  assert code == 0
  data = json.loads(out.read_text(encoding="utf-8"))
  assert data["commit"] == "b" * 40
  assert data["ref"] == "main"
  assert data["run_id"] == "123"
  assert data["run_number"] == "7"
  assert data["workflow"] == "CI"
  assert data["runner_os"] == "Linux"
  assert datetime.fromisoformat(data["generated_at_utc"]) is not None


def test_metadata_missing_environment_becomes_null(tmp_path, monkeypatch):
  for name in (
      "GITHUB_SHA",
      "GITHUB_REF_NAME",
      "GITHUB_REF",
      "GITHUB_RUN_ID",
      "GITHUB_RUN_NUMBER",
      "GITHUB_WORKFLOW",
      "RUNNER_OS",
  ):
    monkeypatch.delenv(name, raising=False)
  out = tmp_path / "metadata.json"

  code = benchmark_artifact_metadata.main(["--out", str(out)])

  assert code == 0
  data = json.loads(out.read_text(encoding="utf-8"))
  assert data["commit"] is None
  assert data["ref"] is None
  assert data["run_id"] is None


def test_metadata_ref_falls_back_to_github_ref(tmp_path, monkeypatch):
  monkeypatch.delenv("GITHUB_REF_NAME", raising=False)
  monkeypatch.setenv("GITHUB_REF", "refs/pull/1/merge")
  out = tmp_path / "metadata.json"

  code = benchmark_artifact_metadata.main(["--out", str(out)])

  assert code == 0
  data = json.loads(out.read_text(encoding="utf-8"))
  assert data["ref"] == "refs/pull/1/merge"


def test_normalized_flags_drop_paths_and_stay_deterministic(tmp_path):
  db = tmp_path / "compile_commands.json"
  db.write_text(
    json.dumps(
      [
        {
          "file": "/repo/bench/tess_bench.cc",
          "command": (
            "/usr/bin/clang++ -I/repo/include -isystem /deps -O3 -DNDEBUG "
            "-std=gnu++20 -o bench/tess_bench.cc.o -c /repo/bench/tess_bench.cc"
          ),
        },
        {
          "file": "/repo/bench/tess_bench.cc",
          "arguments": [
            "clang++", "-I/repo/include", "-O3", "-DNDEBUG",
            "-DTESS_ENABLE_DIAGNOSTICS", "-std=gnu++20",
            "-c", "/repo/bench/tess_bench.cc", "-o", "x.o",
          ],
        },
      ]
    ),
    encoding="utf-8",
  )

  flags = benchmark_artifact_metadata.normalized_flags(db, "tess_bench.cc")

  # Deterministic pick across entry order, paths dropped, order kept.
  assert flags == "-O3 -DNDEBUG -DTESS_ENABLE_DIAGNOSTICS -std=gnu++20"
  assert benchmark_artifact_metadata.normalized_flags(
    tmp_path / "absent.json", "tess_bench.cc"
  ) is None


def test_fingerprint_missing_fields_are_unusable(tmp_path, monkeypatch):
  monkeypatch.delenv("ImageOS", raising=False)
  monkeypatch.delenv("ImageVersion", raising=False)

  fingerprint = benchmark_artifact_metadata.build_fingerprint(tmp_path)

  assert fingerprint["usable"] is False
  assert fingerprint["key"] is None


def test_fingerprint_complete_fields_key_deterministically(
  tmp_path, monkeypatch
):
  monkeypatch.setenv("ImageOS", "ubuntu24")
  monkeypatch.setenv("ImageVersion", "20260729.1")
  monkeypatch.setattr(
    benchmark_artifact_metadata, "cpu_model", lambda: "TestCPU"
  )
  monkeypatch.setattr(
    benchmark_artifact_metadata,
    "compiler_version",
    lambda _c: "clang version 19",
  )
  (tmp_path / "compile_commands.json").write_text(
    json.dumps(
      [
        {
          "file": "/r/bench/tess_bench.cc",
          "command": "c++ -O3 -c /r/bench/tess_bench.cc -o o.o",
        }
      ]
    ),
    encoding="utf-8",
  )

  first = benchmark_artifact_metadata.build_fingerprint(tmp_path)
  second = benchmark_artifact_metadata.build_fingerprint(tmp_path)

  assert first["usable"] is True
  assert first["key"] == second["key"]
  assert first["bench_flags"] == "-O3"


def test_trends_renders_real_time_for_real_time_gated_benchmarks(tmp_path):
  # The parallel pool family is gated on real time because its work runs
  # on worker threads; plotting its CPU time would publish a series the
  # gate and the paired confirmation do not use.
  directory = tmp_path / "artifact"
  directory.mkdir()
  write_json(
      directory / "parallel.json",
      {
          "benchmarks": [
              entry("parallel/tile_touch_pool_w4", 18_000.0,
                    real_time=36_000.0)
          ]
      },
  )
  write_json(directory / "metadata.json", {"commit": "a" * 40})
  thresholds = tmp_path / "thresholds"
  thresholds.mkdir()
  write_json(
      thresholds / "parallel.json",
      {
          "version": 1,
          "benchmarks": {
              "parallel/tile_touch_pool_w4": {
                  "max_real_time_ns": 61_000,
                  "max_cpu_time_ns": None,
              }
          },
      },
  )

  metrics = benchmark_trends.load_threshold_metrics(thresholds)
  values = benchmark_trends.collect_values(
      directory, ("parallel/tile_touch_pool_w4",), metrics=metrics
  )

  assert values == {"parallel/tile_touch_pool_w4": 36_000.0}


def test_trends_keeps_cpu_time_for_everything_else(tmp_path):
  directory = tmp_path / "artifact"
  directory.mkdir()
  write_json(
      directory / "path.json",
      {"benchmarks": [entry("path/sample", 100.0, real_time=999.0)]},
  )
  write_json(directory / "metadata.json", {"commit": "a" * 40})
  thresholds = tmp_path / "thresholds"
  thresholds.mkdir()
  write_json(
      thresholds / "path.json",
      {
          "version": 1,
          "benchmarks": {
              "path/sample": {
                  "max_real_time_ns": None,
                  "max_cpu_time_ns": 500,
              }
          },
      },
  )

  metrics = benchmark_trends.load_threshold_metrics(thresholds)
  values = benchmark_trends.collect_values(
      directory, ("path/sample",), metrics=metrics
  )

  assert values == {"path/sample": 100.0}


def test_trends_refuses_a_missing_thresholds_directory(tmp_path, capsys):
  # Fail closed rather than silently plotting CPU time for a
  # real-time-gated benchmark.
  directory = tmp_path / "artifact"
  directory.mkdir()
  write_json(
      directory / "path.json",
      {"benchmarks": [entry("path/sample", 100.0)]},
  )
  write_json(directory / "metadata.json", {"commit": "a" * 40})

  code = benchmark_trends.main(
      [str(directory), "--thresholds-dir", str(tmp_path / "absent")]
  )

  assert code == 1
  # The distinct message is the point: a directory that does not exist
  # and one that names no benchmarks are different mistakes, and the
  # shared loader would otherwise report both as the latter.
  assert "is not a directory" in capsys.readouterr().err


def test_trends_reports_an_empty_manifest_set_without_a_traceback(
    tmp_path, capsys
):
  directory = tmp_path / "artifact"
  directory.mkdir()
  write_json(
      directory / "path.json",
      {"benchmarks": [entry("path/sample", 100.0)]},
  )
  write_json(directory / "metadata.json", {"commit": "a" * 40})
  empty = tmp_path / "thresholds"
  empty.mkdir()

  code = benchmark_trends.main([str(directory), "--thresholds-dir", str(empty)])

  assert code == 1
  assert "no benchmark thresholds" in capsys.readouterr().err
