#!/usr/bin/env python3
"""Summarize repeated Google Benchmark JSON runs for threshold calibration."""

from __future__ import annotations

import argparse
import json
import math
import statistics
from pathlib import Path
from typing import Any


def main() -> int:
  parser = argparse.ArgumentParser()
  parser.add_argument("results", nargs="+", type=Path)
  parser.add_argument(
      "--headroom",
      default=0.50,
      type=float,
      help="Fractional headroom to add to the maximum observed cpu_time.",
  )
  args = parser.parse_args()

  grouped: dict[str, list[float]] = {}
  for path in args.results:
    collect_cpu_times(path, grouped)

  print("benchmark,samples,median_cpu_ns,max_cpu_ns,cv,suggested_max_cpu_ns")
  for name in sorted(grouped):
    samples = grouped[name]
    median = statistics.median(samples)
    maximum = max(samples)
    mean = statistics.fmean(samples)
    stddev = statistics.pstdev(samples) if len(samples) > 1 else 0.0
    cv = stddev / mean if mean else 0.0
    suggested = math.ceil(maximum * (1.0 + args.headroom))
    print(
        f"{name},{len(samples)},{median:.3f},{maximum:.3f},"
        f"{cv:.4f},{suggested}"
    )

  return 0


def collect_cpu_times(path: Path, grouped: dict[str, list[float]]) -> None:
  data = load_json(path)
  for benchmark in data.get("benchmarks", []):
    name = benchmark.get("name")
    if not isinstance(name, str) or is_aggregate_name(name):
      continue

    value = benchmark.get("cpu_time")
    unit = benchmark.get("time_unit")
    if value is None or unit is None:
      continue

    grouped.setdefault(name, []).append(to_nanoseconds(float(value), str(unit)))


def load_json(path: Path) -> dict[str, Any]:
  with path.open(encoding="utf-8") as file:
    data = json.load(file)
  if not isinstance(data, dict):
    raise TypeError(f"{path} must contain a JSON object")
  return data


def is_aggregate_name(name: str) -> bool:
  return name.endswith(("_mean", "_median", "_stddev", "_cv"))


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
