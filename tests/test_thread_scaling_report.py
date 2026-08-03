"""Tests for the thread-scaling sweep report."""

from __future__ import annotations

import json
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

import thread_scaling_report  # noqa: E402


REPO = Path(__file__).resolve().parents[1]


def _entry(name, real_time, run_type="iteration", aggregate=None):
  entry = {
    "name": name,
    "run_type": run_type,
    "real_time": real_time,
    "cpu_time": real_time / 900.0,
    "time_unit": "ns",
  }
  if aggregate is not None:
    entry["aggregate_name"] = aggregate
  return entry


def _results(
  points, workloads=("chunk_compute",), workers=None, repetitions=None
):
  """A Google Benchmark JSON blob for the given per-point timings.

  Unspecified points scale at exactly the pool's quantization ceiling --
  the fastest a point can legitimately be. Perfect linear scaling would
  exceed the ceiling at 24, 48, 96 and 190 workers and is correctly
  rejected as impossible, so it cannot be the fixture default.
  """
  workers = workers or thread_scaling_report.EXPECTED_WORKERS
  if repetitions is None:
    repetitions = thread_scaling_report.MIN_SAMPLES
  serial_default = 1000.0
  benchmarks = []
  for workload in workloads:
    for _ in range(repetitions):
      benchmarks.append(
        _entry(
          f"lab/thread_scaling/{workload}/serial/real_time",
          points.get((workload, "serial"), serial_default),
        )
      )
    for count in workers:
      ceiling = thread_scaling_report.quantization_ceiling(
        thread_scaling_report.SWEEP_CHUNKS, count
      )
      for _ in range(repetitions):
        benchmarks.append(
          _entry(
            f"lab/thread_scaling/{workload}/{count}/real_time",
            points.get((workload, count), serial_default / ceiling),
          )
        )
  return {"benchmarks": benchmarks}


def _write(tmp_path, blob):
  path = tmp_path / "sweep.json"
  path.write_text(json.dumps(blob), encoding="utf-8")
  return path


# --- Quantization ceiling ---------------------------------------------
#
# The pool claims runs of `stride = max(1, chunks // (workers * 4))`
# chunks, so the reachable speedup is bounded well below `workers` at most
# counts. Publishing a curve without this alongside it invites reading a
# scheduling artifact as a hardware knee.


def test_ceiling_is_exact_when_runs_divide_evenly():
  # 4096 chunks, 32 workers: stride 32, 128 runs, 4 runs each, no tail.
  assert thread_scaling_report.quantization_ceiling(4096, 32) == pytest.approx(
    32.0
  )


def test_ceiling_is_below_worker_count_at_190():
  # stride 5, 820 runs, ceil(820/190) = 5 runs = 25 chunks critical path.
  ceiling = thread_scaling_report.quantization_ceiling(4096, 190)
  assert ceiling == pytest.approx(4096 / 25.0)
  assert ceiling < 190.0


def test_ceiling_fraction_sawtooths_across_worker_counts():
  # The reason it has to be printed: as a fraction of the workers asked
  # for, the ceiling is 100% at 32 and 64 but only ~81% at 48 in between.
  # A curve without it shows a dip at 48 that looks like hardware.
  def fraction(workers):
    return thread_scaling_report.quantization_ceiling(4096, workers) / workers

  assert fraction(32) == pytest.approx(1.0)
  assert fraction(64) == pytest.approx(1.0)
  assert fraction(48) < 0.9
  assert fraction(190) == pytest.approx(0.862, abs=1e-3)


def test_ceiling_of_one_worker_is_one():
  assert thread_scaling_report.quantization_ceiling(4096, 1) == pytest.approx(
    1.0
  )


# --- Expected-point manifest ------------------------------------------
#
# The workload matrix cannot catch a single dropped worker count: its
# family rule still matches the other ten registrations.


def test_missing_worker_point_is_an_error(tmp_path):
  blob = _results(
    {}, workers=[w for w in thread_scaling_report.EXPECTED_WORKERS if w != 96]
  )
  with pytest.raises(thread_scaling_report.ReportError, match="96"):
    thread_scaling_report.load_points(_write(tmp_path, blob))


def test_missing_serial_baseline_is_an_error(tmp_path):
  blob = _results({})
  blob["benchmarks"] = [
    b for b in blob["benchmarks"] if not b["name"].endswith("/serial/real_time")
  ]
  with pytest.raises(thread_scaling_report.ReportError, match="serial"):
    thread_scaling_report.load_points(_write(tmp_path, blob))


def test_complete_sweep_loads(tmp_path):
  points = thread_scaling_report.load_points(_write(tmp_path, _results({})))
  assert set(points) == {"chunk_compute"}
  assert points["chunk_compute"].serial_ns > 0


# --- Metric selection -------------------------------------------------


def test_uses_real_time_not_cpu_time(tmp_path):
  # The dispatcher blocks, so its cpu_time is ~900x smaller. Reading the
  # wrong column would report the pool as almost free.
  blob = _results({("chunk_compute", "serial"): 28_000_000.0})
  points = thread_scaling_report.load_points(_write(tmp_path, blob))
  assert points["chunk_compute"].serial_ns == pytest.approx(28_000_000.0)


def test_aggregate_rows_are_ignored(tmp_path):
  blob = _results({})
  blob["benchmarks"].append(
    _entry(
      "lab/thread_scaling/chunk_compute/4/real_time",
      1.0,
      run_type="aggregate",
      aggregate="mean",
    )
  )
  points = thread_scaling_report.load_points(_write(tmp_path, blob))
  # The bogus 1.0 ns aggregate must not become the reported median.
  assert points["chunk_compute"].by_workers[4].median_ns > 1.0


def test_median_and_cv_come_from_repetitions(tmp_path):
  blob = {"benchmarks": []}
  for value in (100.0, 200.0, 300.0):
    blob["benchmarks"].append(
      _entry("lab/thread_scaling/chunk_fill/4/real_time", value)
    )
  blob["benchmarks"].append(
    _entry("lab/thread_scaling/chunk_fill/serial/real_time", 800.0)
  )
  for count in thread_scaling_report.EXPECTED_WORKERS:
    if count != 4:
      blob["benchmarks"].append(
        _entry(f"lab/thread_scaling/chunk_fill/{count}/real_time", 500.0)
      )
  points = thread_scaling_report.load_points(_write(tmp_path, blob))
  point = points["chunk_fill"].by_workers[4]
  assert point.median_ns == pytest.approx(200.0)
  assert point.samples == 3
  # Sample standard deviation, matching the CV Google Benchmark prints in
  # its own _cv rows, so the two can be compared directly.
  assert point.cv == pytest.approx(0.5, abs=1e-3)


# --- Speedup is measured against serial, never against one worker -----


def test_speedup_denominator_is_the_serial_executor(tmp_path):
  blob = _results(
    {
      ("chunk_fill", "serial"): 1000.0,
      ("chunk_fill", 4): 250.0,
      ("chunk_fill", 1): 500.0,
    },
    workloads=("chunk_fill",),
  )
  points = thread_scaling_report.load_points(_write(tmp_path, blob))
  rows = thread_scaling_report.rows_for(points["chunk_fill"], chunks=4096)
  row = next(r for r in rows if r.workers == 4)
  # 1000/250 against serial, not 500/250 against the one-worker pool.
  assert row.speedup == pytest.approx(4.0)


def test_one_worker_pool_is_reported_separately_from_serial(tmp_path):
  blob = _results(
    {("chunk_fill", "serial"): 1000.0, ("chunk_fill", 1): 1200.0},
    workloads=("chunk_fill",),
  )
  points = thread_scaling_report.load_points(_write(tmp_path, blob))
  rows = thread_scaling_report.rows_for(points["chunk_fill"], chunks=4096)
  row = next(r for r in rows if r.workers == 1)
  assert row.speedup == pytest.approx(1000.0 / 1200.0)


# --- Publishability gate ----------------------------------------------


def test_noisy_points_are_flagged(tmp_path):
  blob = {"benchmarks": []}
  for value in (100.0, 1000.0):
    blob["benchmarks"].append(
      _entry("lab/thread_scaling/chunk_fill/4/real_time", value)
    )
  blob["benchmarks"].append(
    _entry("lab/thread_scaling/chunk_fill/serial/real_time", 800.0)
  )
  for count in thread_scaling_report.EXPECTED_WORKERS:
    if count != 4:
      blob["benchmarks"].append(
        _entry(f"lab/thread_scaling/chunk_fill/{count}/real_time", 500.0)
      )
  points = thread_scaling_report.load_points(_write(tmp_path, blob))
  rows = thread_scaling_report.rows_for(points["chunk_fill"], chunks=4096)
  noisy = [r for r in rows if r.cv > thread_scaling_report.CV_LIMIT]
  assert [r.workers for r in noisy] == [4]


def test_report_exits_nonzero_when_a_point_is_too_noisy(tmp_path, capsys):
  blob = {"benchmarks": []}
  for value in (100.0, 1000.0):
    blob["benchmarks"].append(
      _entry("lab/thread_scaling/chunk_fill/4/real_time", value)
    )
  blob["benchmarks"].append(
    _entry("lab/thread_scaling/chunk_fill/serial/real_time", 800.0)
  )
  for count in thread_scaling_report.EXPECTED_WORKERS:
    if count != 4:
      blob["benchmarks"].append(
        _entry(f"lab/thread_scaling/chunk_fill/{count}/real_time", 500.0)
      )
  status = thread_scaling_report.main([str(_write(tmp_path, blob))])
  assert status == 1
  assert "not publishable" in capsys.readouterr().err


def test_single_repetition_is_not_publishable(tmp_path, capsys):
  # A CV needs samples. With one repetition per point the CV reads 0.00%
  # and the noise gate is vacuous, so a run that measured nothing would
  # otherwise be declared clean.
  blob = _results({}, repetitions=1)
  status = thread_scaling_report.main([str(_write(tmp_path, blob))])
  assert status == 1
  assert "repetition" in capsys.readouterr().err


def test_enough_repetitions_passes(tmp_path, capsys):
  status = thread_scaling_report.main([str(_write(tmp_path, _results({})))])
  assert status == 0, capsys.readouterr().err


def test_speedup_above_the_ceiling_is_reported(tmp_path, capsys):
  # Impossible by construction: it means --chunks does not describe the
  # world the sweep ran on, so every ceiling in the table is wrong.
  # 48 workers: ceiling is 39.0x, so a clean 48x cannot be real.
  blob = _results(
    {("chunk_fill", "serial"): 1000.0, ("chunk_fill", 48): 1000.0 / 48},
    workloads=("chunk_fill",),
  )
  status = thread_scaling_report.main([str(_write(tmp_path, blob))])
  assert status == 1
  assert "exceeds the 39.0x quantization ceiling" in capsys.readouterr().err


# --- Output -----------------------------------------------------------


def test_markdown_reports_ceiling_next_to_speedup(tmp_path):
  status = thread_scaling_report.main([str(_write(tmp_path, _results({})))])
  assert status == 0


def test_markdown_names_the_ceiling_column(tmp_path, capsys):
  thread_scaling_report.main([str(_write(tmp_path, _results({})))])
  out = capsys.readouterr().out
  assert "ceiling" in out.lower()
  assert "speedup" in out.lower()
  assert "cv" in out.lower()


def test_worker_list_matches_the_benchmark_source():
  """The manifest here and the Arg() list in the sweep must agree."""
  source = (REPO / "bench" / "tess_thread_scaling_bench.cc").read_text(
    encoding="utf-8"
  )
  start = source.index("kWorkerCounts{")
  end = source.index("}", start)
  literal = source[start + len("kWorkerCounts{") : end]
  declared = [int(token) for token in literal.replace("\n", "").split(",")]
  assert declared == list(thread_scaling_report.EXPECTED_WORKERS)


def test_chunk_count_matches_the_benchmark_source():
  source = (REPO / "bench" / "tess_thread_scaling_bench.cc").read_text(
    encoding="utf-8"
  )
  assert f"kChunkCount == {thread_scaling_report.SWEEP_CHUNKS}" in source
