"""Tests for the thread-scaling sweep report."""

from __future__ import annotations

import json
import random
import re
import statistics
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
  points, workloads=None, workers=None, repetitions=None, serial=None
):
  """A Google Benchmark JSON blob for the given per-point timings.

  Unspecified points scale at exactly the pool's quantization ceiling --
  the fastest a point can legitimately be. Perfect linear scaling would
  exceed the ceiling at 24, 48, 96 and 190 workers and is correctly
  rejected as impossible, so it cannot be the fixture default.
  """
  workloads = workloads or thread_scaling_report.EXPECTED_WORKLOADS
  workers = workers or thread_scaling_report.EXPECTED_WORKERS
  if repetitions is None:
    repetitions = thread_scaling_report.MIN_SAMPLES
  serial_default = 1000.0
  benchmarks = []
  for workload in workloads:
    for index in range(repetitions):
      # `serial` supplies per-repetition values when a test needs a noisy
      # denominator; otherwise every repetition is identical.
      value = (
        serial[index % len(serial)]
        if serial
        else points.get((workload, "serial"), serial_default)
      )
      benchmarks.append(
        _entry(f"lab/thread_scaling/{workload}/serial/real_time", value)
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


def _padded(blob):
  """Fill in whole workloads a focused fixture left out.

  A sweep artifact must contain every workload, so a fixture that only
  cares about one still has to carry the rest for the report to accept
  it. Tests that are about the manifest itself do not use this.
  """
  present = {
    name.split("/")[2]
    for name in (entry["name"] for entry in blob["benchmarks"])
  }
  missing = [
    w for w in thread_scaling_report.EXPECTED_WORKLOADS if w not in present
  ]
  if missing:
    blob["benchmarks"].extend(_results({}, workloads=missing)["benchmarks"])
  return blob


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
  assert set(points) == set(thread_scaling_report.EXPECTED_WORKLOADS)
  assert points["chunk_compute"].serial_ns > 0


# --- Metric selection -------------------------------------------------


def test_uses_real_time_not_cpu_time(tmp_path):
  # The dispatcher blocks, so its cpu_time is ~900x smaller. Reading the
  # wrong column would report the pool as almost free.
  blob = _results({("chunk_compute", "serial"): 28_000_000.0})
  points = thread_scaling_report.load_points(_write(tmp_path, _padded(blob)))
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
  points = thread_scaling_report.load_points(_write(tmp_path, _padded(blob)))
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
  points = thread_scaling_report.load_points(_write(tmp_path, _padded(blob)))
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
  points = thread_scaling_report.load_points(_write(tmp_path, _padded(blob)))
  rows = thread_scaling_report.rows_for(points["chunk_fill"], chunks=4096)
  row = next(r for r in rows if r.workers == 4)
  # 1000/250 against serial, not 500/250 against the one-worker pool.
  assert row.speedup == pytest.approx(4.0)


def test_one_worker_pool_is_reported_separately_from_serial(tmp_path):
  blob = _results(
    {("chunk_fill", "serial"): 1000.0, ("chunk_fill", 1): 1200.0},
    workloads=("chunk_fill",),
  )
  points = thread_scaling_report.load_points(_write(tmp_path, _padded(blob)))
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
  points = thread_scaling_report.load_points(_write(tmp_path, _padded(blob)))
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
  status = thread_scaling_report.main([str(_write(tmp_path, _padded(blob)))])
  assert status == 1
  assert "not publishable" in capsys.readouterr().err


def test_single_repetition_is_not_publishable(tmp_path, capsys):
  # A CV needs samples. With one repetition per point the CV reads 0.00%
  # and the noise gate is vacuous, so a run that measured nothing would
  # otherwise be declared clean.
  blob = _results({}, repetitions=1)
  status = thread_scaling_report.main([str(_write(tmp_path, _padded(blob)))])
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
  status = thread_scaling_report.main([str(_write(tmp_path, _padded(blob)))])
  assert status == 1
  assert "the 39.0x quantization ceiling" in capsys.readouterr().err


# --- The whole artifact, not just the points it happens to contain -----


def test_missing_workloads_are_an_error(tmp_path):
  # Worker counts were checked only within workloads the artifact
  # contained, so a sweep that lost six of its seven workloads -- the
  # low-work end the crossover depends on -- was accepted as complete.
  blob = _results({}, workloads=("chunk_fill",))
  with pytest.raises(thread_scaling_report.ReportError, match="tile_touch"):
    thread_scaling_report.load_points(_write(tmp_path, blob))


def test_workload_list_matches_the_benchmark_source():
  source = (REPO / "bench" / "tess_thread_scaling_bench.cc").read_text(
    encoding="utf-8"
  )
  registered = set(
    re.findall(r'->Name\("lab/thread_scaling/([a-z0-9_]+)"\)', source)
  )
  assert registered == set(thread_scaling_report.EXPECTED_WORKLOADS)


# --- The serial baseline is gated too ----------------------------------


def test_noisy_serial_baseline_is_not_publishable(tmp_path, capsys):
  # It is the denominator of every speedup in the table, so noise there
  # contaminates the whole workload rather than one point.
  blob = _results({}, serial=[500.0, 1500.0, 1000.0])
  status = thread_scaling_report.main([str(_write(tmp_path, _padded(blob)))])
  assert status == 1
  assert "denominator" in capsys.readouterr().err


# --- The ceiling check accounts for the executor difference ------------


def test_one_worker_pool_beating_serial_is_not_called_impossible(
  tmp_path, capsys
):
  # The pool and the serial executor are different code paths: a dev-box
  # run measured the one-worker pool 10% faster on chunk_fill. Against the
  # bare 1.0x ceiling that reads as impossible at every width.
  advantage = 1.10
  points = {}
  for workload in thread_scaling_report.EXPECTED_WORKLOADS:
    points[(workload, "serial")] = 1000.0
    for count in thread_scaling_report.EXPECTED_WORKERS:
      ceiling = thread_scaling_report.quantization_ceiling(
        thread_scaling_report.SWEEP_CHUNKS, count
      )
      points[(workload, count)] = 1000.0 / (ceiling * advantage)
  status = thread_scaling_report.main([str(_write(tmp_path, _results(points)))])
  assert status == 0, capsys.readouterr().err


def test_speedup_beyond_the_scaled_model_is_still_caught(tmp_path, capsys):
  points = {}
  for workload in thread_scaling_report.EXPECTED_WORKLOADS:
    points[(workload, "serial")] = 1000.0
    for count in thread_scaling_report.EXPECTED_WORKERS:
      ceiling = thread_scaling_report.quantization_ceiling(
        thread_scaling_report.SWEEP_CHUNKS, count
      )
      points[(workload, count)] = 1000.0 / ceiling
  # Way past what the ceiling allows even after any one-worker advantage.
  points[("chunk_fill", 48)] = 1000.0 / 90.0
  status = thread_scaling_report.main([str(_write(tmp_path, _results(points)))])
  assert status == 1
  assert "--chunks likely" in capsys.readouterr().err


# --- Bootstrap confidence intervals ------------------------------------
#
# The speedup is a ratio of two noisy medians. Printed bare it invites
# reading a 3% difference between adjacent points as a knee, so every
# speedup carries an interval and the crossover verdict is derived from
# whether that interval clears 1.0.


def test_ci_is_degenerate_for_identical_samples():
  lo, hi = thread_scaling_report.bootstrap_median_ci([5.0] * 20)
  assert lo == pytest.approx(5.0)
  assert hi == pytest.approx(5.0)


def test_ci_brackets_the_median():
  samples = [10.0, 11.0, 12.0, 13.0, 100.0]
  lo, hi = thread_scaling_report.bootstrap_median_ci(samples)
  assert lo <= statistics.median(samples) <= hi


def test_ci_is_deterministic_for_a_given_seed():
  samples = [10.0, 11.0, 12.0, 13.0, 100.0]
  first = thread_scaling_report.bootstrap_median_ci(samples, seed=7)
  second = thread_scaling_report.bootstrap_median_ci(samples, seed=7)
  assert first == second


def test_ci_narrows_with_more_samples():
  rng = random.Random(1)
  few = [rng.gauss(100.0, 10.0) for _ in range(5)]
  many = [rng.gauss(100.0, 10.0) for _ in range(200)]
  wide = thread_scaling_report.bootstrap_median_ci(few)
  narrow = thread_scaling_report.bootstrap_median_ci(many)
  assert (narrow[1] - narrow[0]) < (wide[1] - wide[0])


def test_speedup_ci_excludes_one_when_clearly_faster():
  serial = [1000.0] * 20
  pool = [250.0] * 20
  lo, hi = thread_scaling_report.bootstrap_ratio_ci(serial, pool)
  assert lo > 1.0
  assert lo <= 4.0 <= hi


def test_speedup_ci_includes_one_when_indistinguishable():
  rng = random.Random(3)
  serial = [rng.gauss(1000.0, 100.0) for _ in range(20)]
  pool = [rng.gauss(1000.0, 100.0) for _ in range(20)]
  lo, hi = thread_scaling_report.bootstrap_ratio_ci(serial, pool)
  assert lo < 1.0 < hi


def test_verdict_reports_the_crossover_direction():
  # Verdict is (speedup, Holm-adjusted p): direction from the estimate,
  # licence to state it from the corrected p-value.
  assert thread_scaling_report.verdict(1.4, 0.001) == "faster"
  assert thread_scaling_report.verdict(0.3, 0.001) == "slower"
  assert thread_scaling_report.verdict(1.4, 0.20) == "unresolved"
  assert thread_scaling_report.verdict(0.3, 0.20) == "unresolved"


def test_pvalue_floor_does_not_depend_on_the_resample_budget():
  # The defect this replaced: a naive count/resamples p-value clamped to
  # 1/resamples made the Holm-adjusted verdict a function of the Monte
  # Carlo budget. The same 20 observations read `unresolved` at 1,000
  # resamples and `slower` at 10,000, which is not a result.
  serial = [1000.0] * 20
  pool = [250.0] * 20
  small = thread_scaling_report.bootstrap_ratio_p(serial, pool, resamples=1000)
  large = thread_scaling_report.bootstrap_ratio_p(serial, pool, resamples=20000)
  # Both are at their resolution floor; neither claims more than it can.
  assert small == pytest.approx(thread_scaling_report.resolution_floor(1000))
  assert large == pytest.approx(thread_scaling_report.resolution_floor(20000))
  assert small > large  # more resamples resolve further, and say so


def test_pvalue_is_never_zero():
  p = thread_scaling_report.bootstrap_ratio_p([1e9] * 20, [1.0] * 20)
  assert p > 0.0


def test_required_resamples_makes_a_corrected_verdict_reachable():
  need = thread_scaling_report.required_resamples(77)
  assert thread_scaling_report.resolution_floor(need) * 77 <= 0.05
  # One fewer and the tightest possible corrected p misses the threshold.
  assert thread_scaling_report.resolution_floor(need // 2) * 77 > 0.05


def test_default_budget_is_adequate_for_a_full_sweep():
  comparisons = len(thread_scaling_report.EXPECTED_WORKLOADS) * len(
    thread_scaling_report.EXPECTED_WORKERS
  )
  assert thread_scaling_report.BOOTSTRAP_RESAMPLES >= (
    thread_scaling_report.required_resamples(comparisons)
  )


def test_too_few_resamples_is_refused_rather_than_silently_unresolved(
  tmp_path, capsys
):
  status = thread_scaling_report.main([
    str(_write(tmp_path, _results({}))),
    "--bootstrap-resamples",
    "100",
  ])
  assert status == 1
  assert "resamples" in capsys.readouterr().err


# --- Pairing the serial baseline with each point's own process --------


def _per_point_files(tmp_path, serial_for_width):
  """One file per width, each carrying its own serial run.

  The shape a pinned campaign produces: every point is a separate
  process, and each re-measures the serial baseline so the ratio does not
  straddle a process boundary.
  """
  full = _results({})
  paths = []
  for width in thread_scaling_report.EXPECTED_WORKERS:
    out = []
    for entry in full["benchmarks"]:
      if entry["name"].endswith("/serial/real_time"):
        out.append(dict(entry, real_time=serial_for_width(width)))
      elif entry["name"].endswith(f"/{width}/real_time"):
        out.append(entry)
    path = tmp_path / f"w{width}.json"
    path.write_text(json.dumps({"benchmarks": out}))
    paths.append(path)
  return paths


def test_paired_serial_is_preferred_over_the_pooled_one(tmp_path):
  # One process per point means an unpaired ratio straddles a process
  # boundary, and process identity is confounded with worker count.
  paths = _per_point_files(tmp_path, lambda w: 1000.0 + w)
  wl = thread_scaling_report.load_points(paths)["chunk_compute"]
  assert wl.serial_for(2).median_ns == pytest.approx(1002.0)
  assert wl.serial_for(96).median_ns == pytest.approx(1096.0)
  # The pooled baseline mixes every process and would be wrong for both.
  assert wl.serial.samples > wl.serial_for(2).samples


def test_paired_serial_changes_the_computed_speedup(tmp_path):
  paths = _per_point_files(tmp_path, lambda w: 1000.0 + w * 100.0)
  wl = thread_scaling_report.load_points(paths)["chunk_compute"]
  rows = thread_scaling_report.rows_for(wl, chunks=4096, resamples=500)
  row = next(r for r in rows if r.workers == 2)
  assert row.speedup == pytest.approx(1200.0 / row.median_ns)


def test_unpaired_artifact_still_uses_the_pooled_serial(tmp_path):
  points = thread_scaling_report.load_points(_write(tmp_path, _results({})))
  wl = points["chunk_compute"]
  # One file: the paired baseline IS the pooled one, by construction.
  assert wl.serial_for(4).samples_ns == wl.serial.samples_ns


def test_a_point_measured_twice_is_an_error(tmp_path):
  a = tmp_path / "a.json"
  b = tmp_path / "b.json"
  blob = _results({})
  a.write_text(json.dumps(blob))
  b.write_text(json.dumps(blob))
  with pytest.raises(thread_scaling_report.ReportError, match="once"):
    thread_scaling_report.load_points([a, b])


def test_holm_is_monotone_and_never_shrinks_a_pvalue():
  raw = [0.001, 0.01, 0.04, 0.5]
  adj = thread_scaling_report.holm_adjust(raw)
  assert all(a >= r for a, r in zip(adj, raw))
  assert adj == sorted(adj)


def test_holm_of_a_single_test_is_the_pvalue():
  assert thread_scaling_report.holm_adjust([0.02]) == [pytest.approx(0.02)]


def test_holm_caps_at_one():
  assert max(thread_scaling_report.holm_adjust([0.5, 0.6, 0.9])) <= 1.0


def test_bootstrap_p_is_small_when_clearly_different():
  p = thread_scaling_report.bootstrap_ratio_p([1000.0] * 20, [250.0] * 20)
  assert p <= 2.0 / thread_scaling_report.BOOTSTRAP_RESAMPLES


def test_bootstrap_p_is_large_when_indistinguishable():
  rng = random.Random(11)
  a = [rng.gauss(1000.0, 100.0) for _ in range(20)]
  b = [rng.gauss(1000.0, 100.0) for _ in range(20)]
  assert thread_scaling_report.bootstrap_ratio_p(a, b) > 0.05


def test_correction_spans_all_workloads_not_one_table(tmp_path):
  # The crossover is read across workloads, so the family of tests is the
  # whole artifact; correcting per table would leave the sweep uncorrected.
  points = thread_scaling_report.load_points(_write(tmp_path, _results({})))
  rows = {
    n: thread_scaling_report.rows_for(points[n], chunks=4096, resamples=2000)
    for n in points
  }
  thread_scaling_report.adjust_across_workloads(rows)
  flat = [r for rs in rows.values() for r in rs]
  assert len(flat) == len(thread_scaling_report.EXPECTED_WORKLOADS) * len(
    thread_scaling_report.EXPECTED_WORKERS
  )
  assert all(r.adjusted_p >= r.pvalue for r in flat)


def test_multiple_result_files_reassemble_into_one_sweep(tmp_path):
  # A pinned campaign runs one process per point, so the report must
  # accept the per-point files and treat them as a single sweep.
  full = _results({})
  half = len(full["benchmarks"]) // 2
  a = tmp_path / "a.json"
  b = tmp_path / "b.json"
  a.write_text(json.dumps({"benchmarks": full["benchmarks"][:half]}))
  b.write_text(json.dumps({"benchmarks": full["benchmarks"][half:]}))
  points = thread_scaling_report.load_points([a, b])
  assert set(points) == set(thread_scaling_report.EXPECTED_WORKLOADS)


def test_rows_carry_a_speedup_interval(tmp_path):
  points = thread_scaling_report.load_points(_write(tmp_path, _results({})))
  rows = thread_scaling_report.rows_for(points["chunk_compute"], chunks=4096)
  row = next(r for r in rows if r.workers == 4)
  assert row.speedup_lo <= row.speedup <= row.speedup_hi


def test_markdown_shows_the_interval_and_verdict(tmp_path, capsys):
  thread_scaling_report.main([str(_write(tmp_path, _results({})))])
  out = capsys.readouterr().out
  assert "95% CI" in out
  assert "vs serial" in out


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
