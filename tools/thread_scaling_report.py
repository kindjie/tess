#!/usr/bin/env python3
"""Turn a thread-scaling sweep into a publishable markdown report.

Reads the Google Benchmark JSON written by tess_bench_thread_scaling and
emits one table per workload: median wall time, speedup against the
serial executor, the pool's deterministic quantization ceiling, and the
per-point coefficient of variation.

Three things here are deliberate and load-bearing.

`real_time`, not `cpu_time`. The dispatching thread blocks for the whole
phase, so its cpu_time is roughly three orders of magnitude smaller than
the wall time; reading the wrong column reports the pool as nearly free.
This is also why tools/benchmark_trends.py cannot render this sweep --
it reads cpu_time only, and its data model is one scalar per benchmark
name across artifacts rather than a parameter sweep.

Speedup is measured against the serial executor at the same world size,
never against the one-worker pool. A one-worker pool still pays dispatch
and a handoff to a worker thread, and on some workloads it is measurably
faster than serial rather than equal to it.

The quantization ceiling is printed next to every measurement. The pool
claims runs of `stride = max(1, chunks // (workers * 4))` chunks, so the
reachable speedup is capped below the worker count at most widths, and as
a fraction of the workers asked for that cap sawtooths: 100% at 32 and 64
workers but only 81% at 48 in between, and 86% at 190. Without the
ceiling in the same row, the dip at 48 reads as a hardware knee.
"""

from __future__ import annotations

import argparse
import json
import math
import random
import statistics
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

# Must match kWorkerCounts in bench/tess_thread_scaling_bench.cc. The
# workload matrix cannot enforce this: its family rule still matches when
# a single worker count is dropped, so the point set is checked here.
EXPECTED_WORKERS: tuple[int, ...] = (1, 2, 4, 8, 16, 24, 32, 48, 64, 96, 190)

# Must match the registrations in the same file. Checking worker counts
# only within the workloads an artifact happens to contain would accept a
# sweep that lost six of its seven workloads -- including, silently, the
# low-work points the crossover result depends on.
EXPECTED_WORKLOADS: tuple[str, ...] = (
  "chunk_compute",
  "chunk_fill",
  "partial_fill_1536",
  "partial_fill_192",
  "partial_fill_64",
  "partial_fill_640",
  "tile_touch",
)

# Must match SweepTraits in the same file.
SWEEP_CHUNKS = 4096

# Runs per worker the pool aims for when sizing a claim; see
# include/tess/ops/phase_executor.h.
RUNS_PER_WORKER = 4

# A curve with wider dispersion than this cannot distinguish a real knee
# from noise, so the report refuses to call it publishable.
CV_LIMIT = 0.05

# A CV needs samples to mean anything. Below two it is undefined and this
# tool reports 0.0, which would sail through the CV gate -- a one-shot run
# would be declared publishable precisely because it measured nothing.
# The campaign uses 20; this is the floor at which the gate is honest.
MIN_SAMPLES = 3

# How far a measured speedup may exceed the modelled maximum before it is
# reported as impossible. Usually that means a --chunks that does not
# describe the world the sweep ran on, which silently invalidates every
# ceiling in the table.
#
# The model is `ceiling(workers) * one_worker_speedup`, not the ceiling
# alone. The pool and the serial executor are different code paths, so
# the pool can beat serial for reasons that have nothing to do with
# parallelism -- a dev-box run measured the ONE-worker pool 10% faster
# than serial on chunk_fill. Against the bare ceiling that reading is
# "impossible" at every width; against this model it is just the pool's
# non-parallel advantage, and the ceiling bounds only the parallel gain
# on top of it.
CEILING_TOLERANCE = 0.10

NAME_PREFIX = "lab/thread_scaling/"
CONTROL_SUFFIX = "/real_time"

# Percentile bootstrap settings. Seeded, because a report that prints
# different intervals each time it is run over the same artifact cannot
# be quoted in a document or diffed against a later campaign.
BOOTSTRAP_RESAMPLES = 10_000
BOOTSTRAP_SEED = 20260802
CONFIDENCE = 0.95


class ReportError(Exception):
  """Input error that should be reported without a traceback."""


@dataclass
class Point:
  """Repetitions for one (workload, worker count) pair."""

  samples_ns: list[float] = field(default_factory=list)

  @property
  def samples(self) -> int:
    return len(self.samples_ns)

  @property
  def median_ns(self) -> float:
    return statistics.median(self.samples_ns)

  @property
  def cv(self) -> float:
    if len(self.samples_ns) < 2:
      return 0.0
    mean = statistics.fmean(self.samples_ns)
    if not mean:
      return 0.0
    return statistics.stdev(self.samples_ns) / mean


@dataclass
class Workload:
  """The serial baseline and every pool point for one workload."""

  name: str
  serial: Point = field(default_factory=Point)
  by_workers: dict[int, Point] = field(default_factory=dict)
  # Serial samples measured in the SAME process as each width's pool
  # point, when the campaign paired them; see load_points.
  serial_by_width: dict[int, Point] = field(default_factory=dict)

  def serial_for(self, workers: int) -> Point:
    return self.serial_by_width.get(workers) or self.serial

  @property
  def serial_ns(self) -> float:
    return self.serial.median_ns


@dataclass
class Row:
  workers: int
  median_ns: float
  speedup: float
  speedup_lo: float
  speedup_hi: float
  ceiling: float
  efficiency: float
  cv: float
  samples: int
  pvalue: float
  adjusted_p: float = 1.0

  @property
  def verdict(self) -> str:
    return verdict(self.speedup, self.adjusted_p)


def _percentiles(values: list[float]) -> tuple[float, float]:
  values.sort()
  lo_index = int((1.0 - CONFIDENCE) / 2.0 * len(values))
  hi_index = min(len(values) - 1, int((1.0 + CONFIDENCE) / 2.0 * len(values)))
  return values[lo_index], values[hi_index]


def bootstrap_median_ci(
  samples: list[float],
  resamples: int = BOOTSTRAP_RESAMPLES,
  seed: int = BOOTSTRAP_SEED,
) -> tuple[float, float]:
  """Percentile bootstrap interval for the median of `samples`.

  Non-parametric on purpose: benchmark timings are right-skewed, so an
  interval derived from a normal assumption would be wrong in the
  direction that matters -- too narrow on the slow tail.
  """
  if not samples:
    raise ReportError("cannot bootstrap an empty sample")
  if len(samples) == 1:
    return samples[0], samples[0]
  rng = random.Random(seed)
  size = len(samples)
  medians = [
    statistics.median(rng.choices(samples, k=size)) for _ in range(resamples)
  ]
  return _percentiles(medians)


def bootstrap_ratio_ci(
  numerator: list[float],
  denominator: list[float],
  resamples: int = BOOTSTRAP_RESAMPLES,
  seed: int = BOOTSTRAP_SEED,
) -> tuple[float, float]:
  """Percentile bootstrap interval for median(numerator)/median(denominator).

  Speedup is a ratio of two independently noisy estimates, and the
  uncertainty of a ratio is not the uncertainty of either half. Resampling
  both and dividing propagates it without assuming a distribution for the
  quotient, which has no closed form worth trusting at these sample sizes.
  """
  if not numerator or not denominator:
    raise ReportError("cannot bootstrap an empty sample")
  rng = random.Random(seed)
  ratios: list[float] = []
  for _ in range(resamples):
    top = statistics.median(rng.choices(numerator, k=len(numerator)))
    bottom = statistics.median(rng.choices(denominator, k=len(denominator)))
    ratios.append(top / bottom if bottom else 0.0)
  return _percentiles(ratios)


def bootstrap_ratio_p(
  numerator: list[float],
  denominator: list[float],
  resamples: int = BOOTSTRAP_RESAMPLES,
  seed: int = BOOTSTRAP_SEED,
) -> float:
  """Two-sided bootstrap p-value for median(num)/median(den) != 1.

  A p-value rather than an extreme quantile of the interval, because
  correcting a percentile bootstrap for 77 comparisons would need the
  0.03rd percentile of the resample distribution -- a quantile 20
  repetitions cannot support no matter how many resamples are drawn.
  Counting how often the resampled ratio lands on the wrong side of 1.0
  degrades gracefully instead.
  """
  if not numerator or not denominator:
    raise ReportError("cannot bootstrap an empty sample")
  rng = random.Random(seed)
  below = 0
  for _ in range(resamples):
    top = statistics.median(rng.choices(numerator, k=len(numerator)))
    bottom = statistics.median(rng.choices(denominator, k=len(denominator)))
    if bottom and top / bottom < 1.0:
      below += 1
  # Plus-one (Davison & Hinkley) rather than count/resamples. The naive
  # ratio returns 0 when no resample lands on the far side, and clamping
  # that to 1/resamples makes the floor -- and therefore the Holm-adjusted
  # verdict -- a function of the Monte Carlo budget rather than of the
  # data. At 1,000 resamples a point read `unresolved` and at 10,000 the
  # same 20 observations read `slower`, which is a defect, not a result.
  tail = min(below, resamples - below)
  return min(1.0, 2.0 * (tail + 1) / (resamples + 1))


def resolution_floor(resamples: int = BOOTSTRAP_RESAMPLES) -> float:
  """Smallest p-value this many resamples can distinguish from zero."""
  return 2.0 / (resamples + 1)


def required_resamples(comparisons: int, alpha: float = 0.05) -> int:
  """Resamples needed before a Holm-corrected verdict is even reachable.

  The most significant of `comparisons` tests must clear `alpha /
  comparisons`, so the floor `2/(B+1)` has to sit below that. Below this
  budget every verdict is `unresolved` no matter what the data says --
  a silent, uniform failure that looks like a cautious result.
  """
  return math.ceil(2.0 * comparisons / alpha)


def holm_adjust(pvalues: list[float]) -> list[float]:
  """Holm-Bonferroni step-down adjustment, preserving input order.

  Every published verdict is one of many comparisons drawn from the same
  artifact. Uncorrected, a handful of borderline calls across a full
  sweep is expected even when nothing is happening, which is exactly the
  kind of reading the crossover claim must not rest on.
  """
  order = sorted(range(len(pvalues)), key=lambda i: pvalues[i])
  adjusted = [0.0] * len(pvalues)
  running = 0.0
  for rank, index in enumerate(order):
    scaled = (len(pvalues) - rank) * pvalues[index]
    running = max(running, min(1.0, scaled))
    adjusted[index] = running
  return adjusted


def verdict(speedup: float, adjusted_p: float, alpha: float = 0.05) -> str:
  """Whether the pool beat serial, after correcting for multiplicity.

  Derived from the corrected p-value rather than the point estimate: a
  1.05x speedup that survives no correction is not evidence the pool
  helps, and the adopter-facing crossover rests on exactly this call.
  """
  if adjusted_p >= alpha:
    return "unresolved"
  return "faster" if speedup > 1.0 else "slower"


def quantization_ceiling(chunks: int, workers: int) -> float:
  """The best speedup the pool's run-claiming can reach at this width.

  Not a hardware property and not a measurement: closed-form from the
  stride the pool picks. It is also scale-invariant, because the stride
  grows with the world -- a bigger world does not smooth it out.
  """
  if workers < 1:
    raise ReportError(f"worker count {workers} is not positive")
  stride = max(1, chunks // (workers * RUNS_PER_WORKER))
  runs = math.ceil(chunks / stride)
  # The critical path is the busiest worker: the most runs any one of
  # them can be handed, times the chunks in a run.
  critical_path = math.ceil(runs / workers) * stride
  return chunks / critical_path


def _parse_file(path: Path) -> list[tuple[str, str, float]]:
  """(workload, tail, real_time) triples from one Google Benchmark JSON."""
  try:
    blob: Any = json.loads(Path(path).read_text(encoding="utf-8"))
  except OSError as error:
    raise ReportError(f"cannot read {path}: {error}") from error
  except json.JSONDecodeError as error:
    raise ReportError(f"{path} is not valid JSON: {error}") from error
  found = blob.get("benchmarks") if isinstance(blob, dict) else None
  if not isinstance(found, list):
    raise ReportError(f"{path} has no 'benchmarks' array")

  triples: list[tuple[str, str, float]] = []
  for entry in found:
    if not isinstance(entry, dict):
      continue
    # Aggregate rows restate the repetitions Google Benchmark already
    # emitted; counting them would weight the median toward whatever
    # aggregates happen to be enabled.
    if entry.get("run_type") != "iteration":
      continue
    name = str(entry.get("name", ""))
    if not name.startswith(NAME_PREFIX):
      continue
    if not name.endswith(CONTROL_SUFFIX):
      # UseRealTime() is required for this family; a name without the
      # suffix means the registration lost it and the timings would be
      # paced by the dispatcher's near-zero cpu_time.
      raise ReportError(
        f"'{name}' is missing the {CONTROL_SUFFIX} suffix; the sweep must "
        "be registered with UseRealTime()"
      )
    stem = name[len(NAME_PREFIX) : -len(CONTROL_SUFFIX)]
    workload_name, _, tail = stem.rpartition("/")
    if not workload_name:
      raise ReportError(f"cannot parse a worker count from '{name}'")
    real_time = entry.get("real_time")
    if not isinstance(real_time, (int, float)):
      raise ReportError(f"'{name}' has no numeric real_time")
    triples.append((workload_name, tail, float(real_time)))
  return triples


def load_points(paths: Path | list[Path]) -> dict[str, Workload]:
  """Parse sweep JSON(s), checking the expected point set is complete.

  Accepts several files because a pinned campaign runs one process per
  point so each can be given its own CPU set.

  Serial baselines are tracked PER FILE as well as pooled. When a point's
  process also measured the serial baseline, the speedup for that point
  is computed against that process's own serial run. Otherwise every
  ratio would straddle a process boundary, and process identity is
  perfectly confounded with worker count in a one-process-per-point
  campaign -- which matters here, because this project has already
  measured a benchmark whose cost moved 73% on a code-layout change.
  """
  if isinstance(paths, (str, Path)):
    paths = [Path(paths)]

  workloads: dict[str, Workload] = {}
  seen_pool: dict[tuple[str, int], Path] = {}
  for path in paths:
    file_serial: dict[str, list[float]] = {}
    file_pool: dict[str, dict[int, list[float]]] = {}
    for workload_name, tail, value in _parse_file(path):
      if tail == "serial":
        file_serial.setdefault(workload_name, []).append(value)
        continue
      try:
        workers = int(tail)
      except ValueError as error:
        raise ReportError(
          f"cannot parse a worker count from '{workload_name}/{tail}'"
        ) from error
      file_pool.setdefault(workload_name, {}).setdefault(workers, []).append(
        value
      )

    for workload_name, samples in file_serial.items():
      workload = workloads.setdefault(workload_name, Workload(workload_name))
      workload.serial.samples_ns.extend(samples)
    for workload_name, widths in file_pool.items():
      workload = workloads.setdefault(workload_name, Workload(workload_name))
      for workers, samples in widths.items():
        key = (workload_name, workers)
        if key in seen_pool:
          # Silently summing them would average two different pinnings
          # or two different runs into one point.
          raise ReportError(
            f"'{workload_name}/{workers}' appears in both {seen_pool[key]} "
            f"and {path}; each point must be measured once"
          )
        seen_pool[key] = path
        workload.by_workers.setdefault(workers, Point()).samples_ns.extend(
          samples
        )
        paired = file_serial.get(workload_name)
        if paired:
          workload.serial_by_width[workers] = Point(list(paired))

  path = paths[0] if len(paths) == 1 else Path(f"{len(paths)} result files")

  if not workloads:
    raise ReportError(f"{path} contains no {NAME_PREFIX} benchmarks")

  absent = [name for name in EXPECTED_WORKLOADS if name not in workloads]
  if absent:
    raise ReportError(
      f"{path} is missing workload(s) {', '.join(absent)}; the work-per-chunk "
      "axis is incomplete and the crossover cannot be read from it"
    )

  for workload in workloads.values():
    if not workload.serial.samples_ns:
      raise ReportError(
        f"workload '{workload.name}' has no serial baseline; speedup has no "
        "denominator, and the one-worker pool is not a substitute"
      )
    missing = [w for w in EXPECTED_WORKERS if w not in workload.by_workers]
    if missing:
      raise ReportError(
        f"workload '{workload.name}' is missing worker point(s) "
        f"{', '.join(str(w) for w in missing)}"
      )
  return workloads


def rows_for(
  workload: Workload, chunks: int, resamples: int = BOOTSTRAP_RESAMPLES
) -> list[Row]:
  rows: list[Row] = []
  for workers in sorted(workload.by_workers):
    point = workload.by_workers[workers]
    # The serial run from this point's own process when the campaign
    # paired them, so the ratio does not straddle a process boundary.
    baseline = workload.serial_for(workers)
    serial_ns = baseline.median_ns
    median = point.median_ns
    speedup = serial_ns / median if median else 0.0
    ceiling = quantization_ceiling(chunks, workers)
    speedup_lo, speedup_hi = bootstrap_ratio_ci(
      baseline.samples_ns, point.samples_ns, resamples=resamples
    )
    pvalue = bootstrap_ratio_p(
      baseline.samples_ns, point.samples_ns, resamples=resamples
    )
    rows.append(
      Row(
        workers=workers,
        median_ns=median,
        speedup=speedup,
        speedup_lo=speedup_lo,
        speedup_hi=speedup_hi,
        ceiling=ceiling,
        # Against the ceiling, not against the worker count: the gap to
        # the ceiling is what the hardware and the memory system explain.
        efficiency=speedup / ceiling if ceiling else 0.0,
        cv=point.cv,
        samples=point.samples,
        pvalue=pvalue,
      )
    )
  return rows


def adjust_across_workloads(rows_by_workload: dict[str, list[Row]]) -> None:
  """Apply the multiplicity correction over every comparison at once.

  Correcting within a workload would still leave the sweep as a whole
  uncorrected, and the crossover claim is read across workloads -- so the
  family of tests is the entire artifact, not one table.
  """
  flat = [row for rows in rows_by_workload.values() for row in rows]
  for row, adjusted in zip(flat, holm_adjust([r.pvalue for r in flat])):
    row.adjusted_p = adjusted


def format_workload(
  workload: Workload,
  rows: list[Row],
  chunks: int,
  resamples: int = BOOTSTRAP_RESAMPLES,
) -> str:
  serial_us = workload.serial_ns / 1000.0
  serial_lo, serial_hi = bootstrap_median_ci(
    workload.serial.samples_ns, resamples=resamples
  )
  lines = [
    f"### {workload.name}",
    "",
    f"Serial baseline: {serial_us:,.1f} us "
    f"(95% CI {serial_lo / 1000.0:,.1f} - {serial_hi / 1000.0:,.1f}) over "
    f"{chunks:,} chunks ({workload.serial_ns / chunks:.1f} ns per chunk), "
    f"{workload.serial.samples} repetitions.",
    "",
    "| workers | median (us) | speedup | 95% CI | adj p | vs serial "
    "| ceiling | of ceiling | CV |",
    "| ---: | ---: | ---: | :---: | ---: | :--- | ---: | ---: | ---: |",
  ]
  for row in rows:
    flag = " !" if row.cv > CV_LIMIT else ""
    lines.append(
      f"| {row.workers} | {row.median_ns / 1000.0:,.1f} | "
      f"{row.speedup:.2f}x | {row.speedup_lo:.2f} - {row.speedup_hi:.2f} | "
      f"{row.adjusted_p:.3f} | {row.verdict} | {row.ceiling:.1f}x | "
      f"{row.efficiency * 100:.0f}% | {row.cv * 100:.2f}%{flag} |"
    )
  return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
  parser = argparse.ArgumentParser(description=__doc__)
  # Several files, because pinning each point to its own CPU set means
  # one Google Benchmark process per point, each writing its own JSON.
  parser.add_argument("results", nargs="+", type=Path)
  parser.add_argument(
    "--chunks",
    type=int,
    default=SWEEP_CHUNKS,
    help="Chunk count the sweep ran over, for the quantization ceiling.",
  )
  parser.add_argument(
    "--bootstrap-resamples",
    type=int,
    default=BOOTSTRAP_RESAMPLES,
    help="Bootstrap resamples per interval.",
  )
  args = parser.parse_args(argv)

  try:
    workloads = load_points(args.results)
    comparisons = sum(len(w.by_workers) for w in workloads.values())
    needed = required_resamples(comparisons)
    if args.bootstrap_resamples < needed:
      # Refused rather than run: below this budget the Holm-corrected
      # p-value cannot drop under alpha for ANY point, so every verdict
      # would read `unresolved` -- a uniform silent failure that looks
      # exactly like a cautious result.
      raise ReportError(
        f"--bootstrap-resamples {args.bootstrap_resamples} cannot resolve "
        f"{comparisons} corrected comparisons; the p-value floor would be "
        f"{resolution_floor(args.bootstrap_resamples):.4f} and every verdict "
        f"would be 'unresolved'. Use at least {needed}"
      )
  except ReportError as error:
    print(f"thread_scaling_report: {error}", file=sys.stderr)
    return 1

  print("## Thread-scaling sweep")
  print()
  print(
    "Wall time (`real_time`); the dispatching thread blocks, so `cpu_time` "
    "understates the pool by roughly three orders of magnitude. Speedup is "
    "against the serial executor at the same world size, not against the "
    "one-worker pool. `ceiling` is the pool's run-claiming quantization "
    "limit at that width -- deterministic, not measured. As a fraction of "
    "the workers asked for it sawtooths (100% at 32 and 64, 81% at 48 in "
    "between), so compare a dip against the ceiling before calling it a "
    "hardware knee."
  )
  print()
  print(
    f"Intervals are {CONFIDENCE * 100:.0f}% percentile bootstrap over "
    f"{args.bootstrap_resamples:,} resamples of the repetitions, seeded so "
    "the same artifact always yields the same interval. They are marginal, "
    "so they are shown for scale and are NOT what the verdict rests on."
  )
  print()
  print(
    "`adj p` is a two-sided bootstrap p-value against a speedup of 1.0, "
    "Holm-corrected across every pool comparison in the artifact, and "
    "`vs serial` is read off it. That correction matters: on the 2026-08-03 "
    "campaign the point that appeared to fix the lower end of the crossover "
    "had a marginal interval of 0.91-0.99 but did not survive correction. "
    "Corrections apply to the whole sweep because the crossover is read "
    "across workloads, not within one table."
  )
  print()
  print(
    "Intervals and p-values describe repetition noise only. They say nothing "
    "about drift shared across a point's repetitions, and benchmark order is "
    "not randomised against the worker axis."
  )
  print()

  rows_by_workload = {
    name: rows_for(
      workloads[name], args.chunks, resamples=args.bootstrap_resamples
    )
    for name in sorted(workloads)
  }
  adjust_across_workloads(rows_by_workload)

  problems: list[str] = []
  for name in sorted(workloads):
    workload = workloads[name]
    rows = rows_by_workload[name]
    table = format_workload(
      workload, rows, args.chunks, resamples=args.bootstrap_resamples
    )
    print(table)
    print()

    # Checked before the CV, because too few samples makes the CV read
    # 0.00% and the noise check vacuous rather than failing.
    if workload.serial.samples < MIN_SAMPLES:
      problems.append(
        f"{name}/serial ({workload.serial.samples} repetition(s), "
        f"need {MIN_SAMPLES})"
      )
    # The serial baseline is the denominator of every speedup in the
    # table, so noise in it contaminates the whole workload rather than
    # one point. Gating only the pool rows lets that pass silently.
    elif workload.serial.cv > CV_LIMIT:
      problems.append(
        f"{name}/serial (CV {workload.serial.cv * 100:.2f}%, and it is the "
        "denominator of every speedup in this table)"
      )

    # The pool's non-parallel advantage over the serial executor, which
    # the quantization ceiling does not describe; see CEILING_TOLERANCE.
    one_worker = next((r.speedup for r in rows if r.workers == 1), 1.0)
    for row in rows:
      if row.samples < MIN_SAMPLES:
        problems.append(
          f"{name}/{row.workers} ({row.samples} repetition(s), "
          f"need {MIN_SAMPLES})"
        )
      elif row.cv > CV_LIMIT:
        problems.append(f"{name}/{row.workers} (CV {row.cv * 100:.2f}%)")
      modelled_max = row.ceiling * one_worker
      if row.speedup > modelled_max * (1.0 + CEILING_TOLERANCE):
        problems.append(
          f"{name}/{row.workers} (speedup {row.speedup:.2f}x exceeds "
          f"{modelled_max:.1f}x, the {row.ceiling:.1f}x quantization ceiling "
          f"scaled by the {one_worker:.2f}x one-worker pool; --chunks likely "
          "does not match the world the sweep ran on)"
        )

  if problems:
    print(
      f"thread_scaling_report: not publishable as a curve; "
      f"{len(problems)} problem(s): " + ", ".join(problems),
      file=sys.stderr,
    )
    return 1
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
