"""Tests for the benchmark change-point detector."""

from __future__ import annotations

import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

import benchmark_changepoint  # noqa: E402


def _artifact(root, run_id, values, key="fp-a", event="push", ref="main",
              usable=True, unit="ns"):
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
    for repetition in range(3):
      rows.append(
        {
          "name": name,
          "run_type": "iteration",
          "time_unit": unit,
          "cpu_time": value,
        }
      )
    rows.append(
      {
        "name": name,
        "run_type": "aggregate",
        "time_unit": unit,
        "cpu_time": value * 100,  # must be ignored
      }
    )
  (directory / "bench.json").write_text(
    json.dumps({"benchmarks": rows}), encoding="utf-8"
  )


def _detect(root, **kwargs):
  artifacts = benchmark_changepoint.load_history(root)
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


def test_stable_series_is_clean(tmp_path):
  for run in range(12):
    _artifact(tmp_path, 100 + run, {"path/x": 10_000.0 + (run % 3) * 50})

  result = _detect(tmp_path)

  assert result["verdict"] == "clean"


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
