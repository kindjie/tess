"""Tests for the benchmark change-point detector."""

from __future__ import annotations

import json
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

import benchmark_changepoint  # noqa: E402


def _artifact(root, run_id, values, key="fp-a", event="push", ref="main",
              usable=True, unit="ns", real_values=None):
  directory = root / str(run_id)
  directory.mkdir()
  (directory / "metadata.json").write_text(
    json.dumps(
      {
        "run_id": str(run_id),
        "run_attempt": "1",
        "commit": f"c{run_id}",
        "event_name": event,
        "ref": ref,
        "fingerprint": {"usable": usable, "key": key if usable else None},
      }
    ),
    encoding="utf-8",
  )
  rows = []
  for name, value in values.items():
    # Real time defaults to CPU time so metric-agnostic cases stay
    # unaffected; a case that cares sets the two apart explicitly.
    real = (real_values or {}).get(name, value)
    for repetition in range(3):
      rows.append(
        {
          "name": name,
          "run_type": "iteration",
          "time_unit": unit,
          "cpu_time": value,
          "real_time": real,
        }
      )
    rows.append(
      {
        "name": name,
        "run_type": "aggregate",
        "time_unit": unit,
        "cpu_time": value * 100,  # must be ignored
        "real_time": real * 100,
      }
    )
  (directory / "bench.json").write_text(
    json.dumps({"benchmarks": rows}), encoding="utf-8"
  )


def _thresholds_dir(root, entries):
  """Write a thresholds manifest mapping name to its gated ceilings."""
  directory = root / "thresholds"
  directory.mkdir(exist_ok=True)
  (directory / "family.json").write_text(
    json.dumps({"version": 1, "benchmarks": entries}), encoding="utf-8"
  )
  return directory


def _detect(root, **kwargs):
  artifacts = benchmark_changepoint.load_history(
    root, metrics=kwargs.get("metrics")
  )
  return benchmark_changepoint.detect(
    artifacts,
    relative_floor=kwargs.get("relative_floor", 0.10),
    absolute_floor_ns=kwargs.get("absolute_floor_ns", 2000.0),
  )


def test_insufficient_history_never_alerts(tmp_path):
  for run in range(5):
    _artifact(tmp_path, 100 + run, {"path/x": 10_000.0})

  result = _detect(tmp_path)

  assert result["verdict"] == "insufficient-history"
  assert result["suspects"] == []
  assert result["candidate_count"] == 1
  assert result["evaluated_count"] == 0
  assert result["not_evaluated"][0]["reason"] == (
    "insufficient-stratum-history"
  )


def test_no_data_has_uniform_coverage_fields():
  result = benchmark_changepoint.detect(
    [], relative_floor=0.10, absolute_floor_ns=2000.0
  )

  assert result == {
    "verdict": "no-data",
    "stratum_size": 0,
    "candidate_count": 0,
    "evaluated_count": 0,
    "not_evaluated": [],
    "suspects": [],
  }


def test_stable_series_is_clean(tmp_path):
  for run in range(12):
    _artifact(tmp_path, 100 + run, {"path/x": 10_000.0 + (run % 3) * 50})

  result = _detect(tmp_path)

  assert result["verdict"] == "clean"
  assert result["candidate_count"] == 1
  assert result["evaluated_count"] == 1
  assert result["not_evaluated"] == []


def test_missing_from_newest_candidate_is_partial(tmp_path):
  values = {"path/stable": 10_000.0, "path/removed": 10_000.0}
  for run in range(10):
    _artifact(tmp_path, 100 + run, values)
  _artifact(tmp_path, 110, {"path/stable": 10_000.0})

  result = _detect(tmp_path)

  assert result["verdict"] == "partial"
  assert result["candidate_count"] == 2
  assert result["evaluated_count"] == 1
  assert result["not_evaluated"] == [
    {
      "benchmark": "path/removed",
      "reason": "missing-candidate",
      "missing_run_ids": [110],
    }
  ]


def test_short_per_benchmark_history_is_partial(tmp_path):
  for run in range(8):
    values = {"path/eight": 10_000.0}
    if run > 0:
      values["path/seven"] = 10_000.0
    _artifact(tmp_path, 100 + run, values)
  for run in range(8, 11):
    _artifact(
      tmp_path,
      100 + run,
      {"path/eight": 10_000.0, "path/seven": 10_000.0},
    )

  result = _detect(tmp_path)

  assert result["verdict"] == "partial"
  assert result["evaluated_count"] == 1
  assert result["not_evaluated"] == [
    {
      "benchmark": "path/seven",
      "reason": "insufficient-baseline",
      "baseline_artifacts": 7,
      "required_baseline_artifacts": 8,
    }
  ]


def test_rename_across_candidates_leaves_both_names_unevaluable(tmp_path):
  for run in range(8):
    _artifact(tmp_path, 100 + run, {"path/old": 10_000.0})
  _artifact(tmp_path, 108, {"path/old": 10_000.0})
  for run in (109, 110):
    _artifact(tmp_path, run, {"path/new": 10_000.0})

  result = _detect(tmp_path)

  assert result["verdict"] == "insufficient-history"
  assert result["candidate_count"] == 2
  assert result["evaluated_count"] == 0
  assert result["not_evaluated"] == [
    {
      "benchmark": "path/new",
      "reason": "missing-candidate",
      "missing_run_ids": [108],
    },
    {
      "benchmark": "path/old",
      "reason": "missing-candidate",
      "missing_run_ids": [109, 110],
    },
  ]


def test_nothing_evaluable_is_insufficient_history(tmp_path):
  for run in range(8):
    _artifact(tmp_path, 100 + run, {"path/old": 10_000.0})
  for run in range(8, 11):
    _artifact(tmp_path, 100 + run, {"path/new": 10_000.0})

  result = _detect(tmp_path)

  assert result["verdict"] == "insufficient-history"
  assert result["candidate_count"] == 1
  assert result["evaluated_count"] == 0
  assert result["not_evaluated"] == [
    {
      "benchmark": "path/new",
      "reason": "insufficient-baseline",
      "baseline_artifacts": 0,
      "required_baseline_artifacts": 8,
    }
  ]


def test_suspect_wins_while_partial_coverage_remains_visible(tmp_path):
  for run in range(8):
    _artifact(tmp_path, 100 + run, {"path/shift": 10_000.0})
  for run in range(8, 11):
    _artifact(
      tmp_path,
      100 + run,
      {"path/shift": 14_000.0, "path/new": 10_000.0},
    )

  result = _detect(tmp_path)

  assert result["verdict"] == "suspects"
  assert result["evaluated_count"] == 1
  assert [entry["benchmark"] for entry in result["suspects"]] == [
    "path/shift"
  ]
  assert [entry["benchmark"] for entry in result["not_evaluated"]] == [
    "path/new"
  ]


@pytest.mark.parametrize(
  "corruption", ("missing-metric", "invalid-unit", "aggregate-only")
)
def test_unusable_candidate_reading_is_partial(tmp_path, corruption):
  values = {"path/stable": 10_000.0, "path/broken": 10_000.0}
  for run in range(11):
    _artifact(tmp_path, 100 + run, values)
  result_path = tmp_path / "110" / "bench.json"
  data = json.loads(result_path.read_text(encoding="utf-8"))
  if corruption == "aggregate-only":
    data["benchmarks"] = [
      row for row in data["benchmarks"]
      if row["name"] != "path/broken" or row["run_type"] == "aggregate"
    ]
  for row in data["benchmarks"]:
    if row["name"] != "path/broken" or row["run_type"] == "aggregate":
      continue
    if corruption == "missing-metric":
      del row["cpu_time"]
    else:
      row["time_unit"] = "ticks"
  result_path.write_text(json.dumps(data), encoding="utf-8")

  result = _detect(tmp_path)

  assert result["verdict"] == "partial"
  assert result["evaluated_count"] == 1
  assert result["not_evaluated"] == [
    {
      "benchmark": "path/broken",
      "reason": "unusable-candidate-reading",
      "unusable_run_ids": [110],
    }
  ]


def test_sustained_shift_flags_with_commit_range(tmp_path):
  for run in range(9):
    _artifact(tmp_path, 100 + run, {"path/x": 10_000.0})
  for run in range(9, 12):
    _artifact(tmp_path, 100 + run, {"path/x": 14_000.0})

  result = _detect(tmp_path)

  assert result["verdict"] == "suspects"
  assert result["suspects"][0]["benchmark"] == "path/x"
  assert result["last_clean_commit"] == "c108"
  assert result["first_elevated_commit"] == "c109"


def test_single_spike_does_not_flag(tmp_path):
  for run in range(11):
    _artifact(tmp_path, 100 + run, {"path/x": 10_000.0})
  _artifact(tmp_path, 111, {"path/x": 14_000.0})

  assert _detect(tmp_path)["verdict"] == "clean"


def test_shift_below_absolute_floor_does_not_flag(tmp_path):
  # +50% but only 500 ns absolute: immaterial.
  for run in range(9):
    _artifact(tmp_path, 100 + run, {"tiny/x": 1_000.0})
  for run in range(9, 12):
    _artifact(tmp_path, 100 + run, {"tiny/x": 1_500.0})

  assert _detect(tmp_path)["verdict"] == "clean"


def test_new_fingerprint_is_a_series_break(tmp_path):
  for run in range(12):
    _artifact(tmp_path, 100 + run, {"path/x": 10_000.0}, key="fp-a")
  _artifact(tmp_path, 112, {"path/x": 25_000.0}, key="fp-b")

  result = _detect(tmp_path)

  assert result["verdict"] == "series-break"
  assert result["suspects"] == []
  assert result["candidate_count"] == 1
  assert result["evaluated_count"] == 0
  assert result["not_evaluated"][0]["reason"] == (
    "insufficient-stratum-history"
  )


def test_returning_fingerprint_resumes_its_stratum(tmp_path):
  # fp-a and fp-b alternate (heterogeneous fleet); each stratum keeps
  # its own history and a return to fp-a is not a break.
  for run in range(24):
    key = "fp-a" if run % 2 == 0 else "fp-b"
    _artifact(tmp_path, 100 + run, {"path/x": 10_000.0}, key=key)

  result = _detect(tmp_path)

  assert result["verdict"] == "clean"
  assert result["stratum_size"] == 12


def test_unusable_and_non_push_artifacts_are_excluded(tmp_path):
  for run in range(12):
    _artifact(tmp_path, 100 + run, {"path/x": 10_000.0})
  _artifact(tmp_path, 112, {"path/x": 90_000.0}, usable=False)
  _artifact(tmp_path, 113, {"path/x": 90_000.0}, event="schedule")
  _artifact(tmp_path, 114, {"path/x": 90_000.0}, ref="refs/pull/1/merge")

  assert _detect(tmp_path)["verdict"] == "clean"


def test_units_convert_to_nanoseconds(tmp_path):
  for run in range(9):
    _artifact(tmp_path, 100 + run, {"path/x": 10.0}, unit="us")
  for run in range(9, 12):
    _artifact(tmp_path, 100 + run, {"path/x": 14.0}, unit="us")

  result = _detect(tmp_path)

  assert result["verdict"] == "suspects"
  assert result["suspects"][0]["baseline_median_ns"] == 10_000.0


def test_report_names_suspects_and_range(tmp_path):
  for run in range(9):
    _artifact(tmp_path, 100 + run, {"path/x": 10_000.0})
  for run in range(9, 12):
    _artifact(tmp_path, 100 + run, {"path/x": 14_000.0})

  report = benchmark_changepoint.render_report(_detect(tmp_path))

  assert "path/x" in report
  assert "c108" in report
  assert "+40.0%" in report
  assert "Evaluated 1 of 1 candidate benchmarks" in report


def test_partial_report_names_coverage_gap(tmp_path):
  for run in range(8):
    values = {"path/stable": 10_000.0}
    if run == 7:
      values["path/new"] = 10_000.0
    _artifact(tmp_path, 100 + run, values)
  for run in range(8, 11):
    _artifact(
      tmp_path,
      100 + run,
      {"path/stable": 10_000.0, "path/new": 10_000.0},
    )

  report = benchmark_changepoint.render_report(_detect(tmp_path))

  assert "partial" in report
  assert "Evaluated 1 of 2 candidate benchmarks" in report
  assert "path/new" in report
  assert "1 of 8 required baseline artifacts" in report


def test_fingerprinted_artifacts_must_attest_push_and_main(tmp_path):
  # A fingerprinted artifact with absent provenance fields is excluded;
  # absence is only tolerated for pre-fingerprint legacy artifacts.
  for run in range(12):
    _artifact(tmp_path, 100 + run, {"path/x": 10_000.0})
  directory = tmp_path / "999"
  directory.mkdir()
  (directory / "metadata.json").write_text(
    json.dumps(
      {
        "run_id": "999",
        "fingerprint": {"usable": True, "key": "fp-a"},
      }
    ),
    encoding="utf-8",
  )
  (directory / "bench.json").write_text(
    json.dumps(
      {
        "benchmarks": [
          {
            "name": "path/x",
            "run_type": "iteration",
            "time_unit": "ns",
            "cpu_time": 90_000.0,
          }
        ]
      }
    ),
    encoding="utf-8",
  )

  artifacts = benchmark_changepoint.load_history(tmp_path)

  assert [a["run_id"] for a in artifacts if a["run_id"] == 999] == []


def test_returning_underfilled_fingerprint_is_insufficient_history(tmp_path):
  # fp-b has appeared before but lacks history; a return to it is not
  # a series break.
  for run in range(12):
    _artifact(tmp_path, 100 + run, {"path/x": 10_000.0}, key="fp-a")
  _artifact(tmp_path, 112, {"path/x": 10_000.0}, key="fp-b")
  _artifact(tmp_path, 113, {"path/x": 10_000.0}, key="fp-a")
  _artifact(tmp_path, 114, {"path/x": 10_000.0}, key="fp-b")

  assert _detect(tmp_path)["verdict"] == "insufficient-history"


def test_corrupt_benchmark_file_poisons_the_artifact(tmp_path):
  for run in range(12):
    _artifact(tmp_path, 100 + run, {"path/x": 10_000.0})
  (tmp_path / "111" / "extra.json").write_text("truncated{", encoding="utf-8")

  artifacts = benchmark_changepoint.load_history(tmp_path)

  assert [a["run_id"] for a in artifacts if a["run_id"] == 111] == []


def test_report_links_the_profiling_protocol(tmp_path):
  for run in range(9):
    _artifact(tmp_path, 100 + run, {"path/x": 10_000.0})
  for run in range(9, 12):
    _artifact(tmp_path, 100 + run, {"path/x": 14_000.0})

  report = benchmark_changepoint.render_report(_detect(tmp_path))

  assert "profiling protocol" in report
  assert "--suspects=path/x" in report


def test_report_caps_the_paste_ready_suspect_list(tmp_path):
  values_base = {f"path/b{i:03d}": 10_000.0 for i in range(70)}
  values_up = {name: 14_000.0 for name in values_base}
  for run in range(9):
    _artifact(tmp_path, 100 + run, values_base)
  for run in range(9, 12):
    _artifact(tmp_path, 100 + run, values_up)

  report = benchmark_changepoint.render_report(_detect(tmp_path))

  assert "plus 6 more" in report
  assert "caps at 64" in report


def test_report_routes_diagnostics_suspects_separately(tmp_path):
  values_base = {"path/x": 10_000.0, "fields/goalset_build_16": 9_000.0}
  values_up = {"path/x": 14_000.0, "fields/goalset_build_16": 13_000.0}
  for run in range(9):
    _artifact(tmp_path, 100 + run, values_base)
  for run in range(9, 12):
    _artifact(tmp_path, 100 + run, values_up)

  report = benchmark_changepoint.render_report(_detect(tmp_path))

  assert "--suspects=path/x" in report
  assert "fields/goalset_build_16" in report
  assert "not confirmable by the dispatch workflow" in report


def test_real_time_gated_benchmark_is_judged_on_real_time(tmp_path):
  # The parallel pool family gates real time because the work happens on
  # worker threads: the dispatching thread's CPU time understates it.
  # A shift that shows only in real time must still be caught.
  flat_cpu = {"parallel/tile_touch_pool_w4": 18_000.0}
  for run in range(9):
    _artifact(tmp_path, 100 + run, flat_cpu,
              real_values={"parallel/tile_touch_pool_w4": 30_000.0})
  for run in range(9, 12):
    _artifact(tmp_path, 100 + run, flat_cpu,
              real_values={"parallel/tile_touch_pool_w4": 40_000.0})
  thresholds = _thresholds_dir(
    tmp_path,
    {"parallel/tile_touch_pool_w4": {"max_real_time_ns": 61_000,
                                     "max_cpu_time_ns": None}},
  )

  metrics = benchmark_changepoint.load_threshold_metrics(thresholds)
  result = _detect(tmp_path, metrics=metrics)

  assert result["verdict"] == "suspects"
  assert result["suspects"][0]["benchmark"] == "parallel/tile_touch_pool_w4"
  assert result["suspects"][0]["baseline_median_ns"] == 30_000.0


def test_real_time_gated_benchmark_ignores_cpu_time_noise(tmp_path):
  # The converse: CPU time drifting on a real-time-gated benchmark is not
  # evidence, because that metric is not what the family is judged on.
  steady_real = {"parallel/tile_touch_pool_w4": 30_000.0}
  for run in range(9):
    _artifact(tmp_path, 100 + run, {"parallel/tile_touch_pool_w4": 18_000.0},
              real_values=steady_real)
  for run in range(9, 12):
    _artifact(tmp_path, 100 + run, {"parallel/tile_touch_pool_w4": 26_000.0},
              real_values=steady_real)
  thresholds = _thresholds_dir(
    tmp_path,
    {"parallel/tile_touch_pool_w4": {"max_real_time_ns": 61_000,
                                     "max_cpu_time_ns": None}},
  )

  metrics = benchmark_changepoint.load_threshold_metrics(thresholds)

  assert _detect(tmp_path, metrics=metrics)["verdict"] == "clean"


def test_cpu_gated_benchmark_still_uses_cpu_time(tmp_path):
  # Everything not gated on real time keeps the previous behaviour, so
  # the fix cannot silently restate the rest of the suite's history.
  steady_real = {"path/x": 10_000.0}
  for run in range(9):
    _artifact(tmp_path, 100 + run, {"path/x": 10_000.0},
              real_values=steady_real)
  for run in range(9, 12):
    _artifact(tmp_path, 100 + run, {"path/x": 14_000.0},
              real_values=steady_real)
  thresholds = _thresholds_dir(
    tmp_path,
    {"path/x": {"max_real_time_ns": None, "max_cpu_time_ns": 20_000}},
  )

  metrics = benchmark_changepoint.load_threshold_metrics(thresholds)
  result = _detect(tmp_path, metrics=metrics)

  assert result["verdict"] == "suspects"
  assert result["suspects"][0]["benchmark"] == "path/x"


def test_benchmark_absent_from_thresholds_defaults_to_cpu_time(tmp_path):
  # Ungated lab registrations have no manifest entry; they must keep the
  # documented default rather than be dropped from the history.
  steady_real = {"lab/thread_scaling/x": 10_000.0}
  for run in range(9):
    _artifact(tmp_path, 100 + run, {"lab/thread_scaling/x": 10_000.0},
              real_values=steady_real)
  for run in range(9, 12):
    _artifact(tmp_path, 100 + run, {"lab/thread_scaling/x": 14_000.0},
              real_values=steady_real)
  thresholds = _thresholds_dir(
    tmp_path, {"path/other": {"max_real_time_ns": 1, "max_cpu_time_ns": None}}
  )

  metrics = benchmark_changepoint.load_threshold_metrics(thresholds)

  assert _detect(tmp_path, metrics=metrics)["verdict"] == "suspects"


def test_cli_defaults_to_the_repository_threshold_manifests(tmp_path):
  # The CI job passes no --thresholds-dir, so the default must resolve
  # to the real manifests: a silent fallback to CPU time everywhere
  # would restate the real-time-gated families without saying so.
  for run in range(12):
    _artifact(tmp_path, 100 + run, {"path/x": 10_000.0})
  out = tmp_path / "cp.json"

  code = benchmark_changepoint.main(
    ["--artifacts", str(tmp_path), "--json", str(out)]
  )

  assert code == 0
  metrics = benchmark_changepoint.load_threshold_metrics(
    Path(__file__).resolve().parents[1] / "bench" / "thresholds"
  )
  assert metrics["parallel/tile_touch_pool_w4"] == "real_time"


def test_cli_newest_unusable_has_no_stale_analysis_fields(tmp_path):
  for run in range(11):
    _artifact(tmp_path, 100 + run, {"path/x": 10_000.0})
  newest = tmp_path / "999"
  newest.mkdir()
  (newest / "metadata.json").write_text("truncated{", encoding="utf-8")
  thresholds = _thresholds_dir(
    tmp_path,
    {"path/x": {"max_real_time_ns": None, "max_cpu_time_ns": 20_000}},
  )
  out = tmp_path / "cp.json"

  code = benchmark_changepoint.main(
    [
      "--artifacts", str(tmp_path),
      "--thresholds-dir", str(thresholds),
      "--json", str(out),
    ]
  )
  result = json.loads(out.read_text(encoding="utf-8"))

  assert code == 0
  assert result["verdict"] == "newest-unusable"
  assert result["candidate_count"] == 0
  assert result["evaluated_count"] == 0
  assert result["not_evaluated"] == []
  assert result["suspects"] == []
  assert "first_elevated_commit" not in result
  assert "last_clean_commit" not in result


def test_cli_refuses_a_missing_thresholds_directory(tmp_path):
  for run in range(12):
    _artifact(tmp_path, 100 + run, {"path/x": 10_000.0})

  code = benchmark_changepoint.main(
    ["--artifacts", str(tmp_path),
     "--thresholds-dir", str(tmp_path / "absent")]
  )

  assert code == 1


def test_thresholds_directory_without_manifests_is_an_error(tmp_path):
  # Fail closed: an empty or misshapen manifest set would otherwise
  # yield an empty map and silently default the whole suite to CPU
  # time — the exact defect this change exists to remove.
  empty = tmp_path / "thresholds"
  empty.mkdir()

  try:
    benchmark_changepoint.load_threshold_metrics(empty)
  except Exception as error:  # noqa: BLE001 - the type is the assertion
    assert "no benchmark" in str(error).lower()
  else:
    raise AssertionError("an empty thresholds directory must not be accepted")


def test_manifest_without_benchmarks_is_an_error(tmp_path):
  directory = tmp_path / "thresholds"
  directory.mkdir()
  (directory / "family.json").write_text(
    json.dumps({"version": 1, "benchmarks": {}}), encoding="utf-8"
  )

  try:
    benchmark_changepoint.load_threshold_metrics(directory)
  except Exception as error:  # noqa: BLE001 - the type is the assertion
    assert "no benchmark" in str(error).lower()
  else:
    raise AssertionError("a manifest set naming no benchmarks is broken input")


def test_cli_reports_an_empty_manifest_set_without_a_traceback(tmp_path):
  for run in range(12):
    _artifact(tmp_path, 100 + run, {"path/x": 10_000.0})
  empty = tmp_path / "thresholds"
  empty.mkdir()

  code = benchmark_changepoint.main(
    ["--artifacts", str(tmp_path), "--thresholds-dir", str(empty)]
  )

  assert code == 1
