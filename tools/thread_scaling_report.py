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
import statistics
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

# Must match kWorkerCounts in bench/tess_thread_scaling_bench.cc. The
# workload matrix cannot enforce this: its family rule still matches when
# a single worker count is dropped, so the point set is checked here.
EXPECTED_WORKERS: tuple[int, ...] = (1, 2, 4, 8, 16, 24, 32, 48, 64, 96, 190)

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

# Measured speedup cannot exceed the pool's own run-claiming ceiling. If
# it does, the model and the data disagree -- almost always a --chunks
# that does not match the world the sweep actually ran. Allowed a little
# slack so ordinary noise near the ceiling is not reported as impossible.
CEILING_TOLERANCE = 0.10

NAME_PREFIX = "lab/thread_scaling/"
CONTROL_SUFFIX = "/real_time"


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

  @property
  def serial_ns(self) -> float:
    return self.serial.median_ns


@dataclass
class Row:
  workers: int
  median_ns: float
  speedup: float
  ceiling: float
  efficiency: float
  cv: float
  samples: int


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


def load_points(path: Path) -> dict[str, Workload]:
  """Parse a sweep JSON, checking the expected point set is complete."""
  try:
    blob: Any = json.loads(Path(path).read_text(encoding="utf-8"))
  except OSError as error:
    raise ReportError(f"cannot read {path}: {error}") from error
  except json.JSONDecodeError as error:
    raise ReportError(f"{path} is not valid JSON: {error}") from error

  entries = blob.get("benchmarks") if isinstance(blob, dict) else None
  if not isinstance(entries, list):
    raise ReportError(f"{path} has no 'benchmarks' array")

  workloads: dict[str, Workload] = {}
  for entry in entries:
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

    workload = workloads.setdefault(workload_name, Workload(workload_name))
    if tail == "serial":
      workload.serial.samples_ns.append(float(real_time))
    else:
      try:
        workers = int(tail)
      except ValueError as error:
        raise ReportError(
          f"cannot parse a worker count from '{name}'"
        ) from error
      workload.by_workers.setdefault(workers, Point()).samples_ns.append(
        float(real_time)
      )

  if not workloads:
    raise ReportError(f"{path} contains no {NAME_PREFIX} benchmarks")

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


def rows_for(workload: Workload, chunks: int) -> list[Row]:
  serial_ns = workload.serial_ns
  rows: list[Row] = []
  for workers in sorted(workload.by_workers):
    point = workload.by_workers[workers]
    median = point.median_ns
    speedup = serial_ns / median if median else 0.0
    ceiling = quantization_ceiling(chunks, workers)
    rows.append(
      Row(
        workers=workers,
        median_ns=median,
        speedup=speedup,
        ceiling=ceiling,
        # Against the ceiling, not against the worker count: the gap to
        # the ceiling is what the hardware and the memory system explain.
        efficiency=speedup / ceiling if ceiling else 0.0,
        cv=point.cv,
        samples=point.samples,
      )
    )
  return rows


def format_workload(workload: Workload, chunks: int) -> tuple[str, list[Row]]:
  rows = rows_for(workload, chunks)
  serial_us = workload.serial_ns / 1000.0
  lines = [
    f"### {workload.name}",
    "",
    f"Serial baseline: {serial_us:,.1f} us over {chunks:,} chunks "
    f"({workload.serial_ns / chunks:.1f} ns per chunk), "
    f"{workload.serial.samples} repetitions.",
    "",
    "| workers | median (us) | speedup | ceiling | of ceiling | CV |",
    "| ---: | ---: | ---: | ---: | ---: | ---: |",
  ]
  for row in rows:
    flag = " !" if row.cv > CV_LIMIT else ""
    lines.append(
      f"| {row.workers} | {row.median_ns / 1000.0:,.1f} | "
      f"{row.speedup:.2f}x | {row.ceiling:.1f}x | "
      f"{row.efficiency * 100:.0f}% | {row.cv * 100:.2f}%{flag} |"
    )
  return "\n".join(lines), rows


def main(argv: list[str] | None = None) -> int:
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("results", type=Path)
  parser.add_argument(
    "--chunks",
    type=int,
    default=SWEEP_CHUNKS,
    help="Chunk count the sweep ran over, for the quantization ceiling.",
  )
  args = parser.parse_args(argv)

  try:
    workloads = load_points(args.results)
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

  problems: list[str] = []
  for name in sorted(workloads):
    workload = workloads[name]
    table, rows = format_workload(workload, args.chunks)
    print(table)
    print()

    # Checked before the CV, because too few samples makes the CV read
    # 0.00% and the noise check vacuous rather than failing.
    if workload.serial.samples < MIN_SAMPLES:
      problems.append(
        f"{name}/serial ({workload.serial.samples} repetition(s), "
        f"need {MIN_SAMPLES})"
      )
    for row in rows:
      if row.samples < MIN_SAMPLES:
        problems.append(
          f"{name}/{row.workers} ({row.samples} repetition(s), "
          f"need {MIN_SAMPLES})"
        )
      elif row.cv > CV_LIMIT:
        problems.append(f"{name}/{row.workers} (CV {row.cv * 100:.2f}%)")
      if row.speedup > row.ceiling * (1.0 + CEILING_TOLERANCE):
        problems.append(
          f"{name}/{row.workers} (speedup {row.speedup:.2f}x exceeds the "
          f"{row.ceiling:.1f}x quantization ceiling; --chunks likely wrong)"
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
