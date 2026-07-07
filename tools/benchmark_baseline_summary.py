#!/usr/bin/env python3
"""Summarize repeated Google Benchmark JSON runs for threshold calibration."""

from __future__ import annotations

import argparse
import csv
import json
import math
import statistics
import sys
from pathlib import Path
from typing import Any


class ToolError(Exception):
  """Input error that should be reported without a traceback."""


def main(argv: list[str] | None = None) -> int:
  parser = argparse.ArgumentParser()
  parser.add_argument("results", nargs="+", type=Path)
  parser.add_argument(
      "--headroom",
      default=0.50,
      type=float,
      help="Fractional headroom to add to the maximum observed cpu_time.",
  )
  args = parser.parse_args(argv)

  grouped: dict[str, list[float]] = {}
  try:
    for path in args.results:
      collect_cpu_times(path, grouped)
  except ToolError as error:
    print(f"benchmark_baseline_summary: {error}", file=sys.stderr)
    return 1

  writer = csv.writer(sys.stdout, lineterminator="\n")
  writer.writerow(
      [
          "benchmark",
          "samples",
          "median_cpu_ns",
          "max_cpu_ns",
          "cv",
          "suggested_max_cpu_ns",
      ]
  )
  for name in sorted(grouped):
    samples = grouped[name]
    median = statistics.median(samples)
    maximum = max(samples)
    mean = statistics.fmean(samples)
    stddev = statistics.pstdev(samples) if len(samples) > 1 else 0.0
    cv = stddev / mean if mean else 0.0
    suggested = math.ceil(maximum * (1.0 + args.headroom))
    writer.writerow(
        [
            name,
            len(samples),
            f"{median:.3f}",
            f"{maximum:.3f}",
            f"{cv:.4f}",
            suggested,
        ]
    )

  return 0


def collect_cpu_times(path: Path, grouped: dict[str, list[float]]) -> None:
  data = load_json(path)
  for benchmark in data.get("benchmarks", []):
    name = benchmark.get("name")
    if not isinstance(name, str):
      continue
    # Repetition aggregates (mean/median/stddev/cv/...) are derived values;
    # only raw repetition samples feed the calibration statistics.
    if benchmark.get("run_type") == "aggregate":
      continue

    value = benchmark.get("cpu_time")
    unit = benchmark.get("time_unit")
    if value is None or unit is None:
      continue

    grouped.setdefault(name, []).append(to_nanoseconds(float(value), str(unit)))


def load_json(path: Path) -> dict[str, Any]:
  try:
    with path.open(encoding="utf-8") as file:
      data = json.load(file)
  except OSError as error:
    raise ToolError(f"{path}: cannot read file: {error}") from error
  except json.JSONDecodeError as error:
    raise ToolError(f"{path}: malformed JSON: {error}") from error
  if not isinstance(data, dict):
    raise ToolError(f"{path} must contain a JSON object")
  return data


def to_nanoseconds(value: float, unit: str) -> float:
  if unit == "ns":
    return value
  if unit == "us":
    return value * 1_000.0
  if unit == "ms":
    return value * 1_000_000.0
  if unit == "s":
    return value * 1_000_000_000.0
  raise ValueError(f"unsupported benchmark time unit: {unit}")


if __name__ == "__main__":
  raise SystemExit(main())
