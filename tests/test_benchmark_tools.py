"""Unit tests for the benchmark threshold and baseline summary tools."""

from __future__ import annotations

import csv
import io
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

import benchmark_baseline_summary  # noqa: E402
import benchmark_thresholds  # noqa: E402


def entry(
    name: str,
    cpu_time: float,
    *,
    time_unit: str = "ns",
    run_type: str = "iteration",
    aggregate_name: str | None = None,
    run_name: str | None = None,
) -> dict:
  benchmark = {
      "name": name,
      "run_name": run_name if run_name is not None else name,
      "run_type": run_type,
      "real_time": cpu_time,
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


def test_null_thresholds_are_skipped(tmp_path):
  benchmarks = [entry("key/free", 1e12)]
  thresholds = {"benchmarks": {"key/free": limits(None)}}

  assert run_thresholds(tmp_path, benchmarks, thresholds) == 0


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
