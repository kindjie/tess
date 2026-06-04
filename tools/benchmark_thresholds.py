#!/usr/bin/env python3
"""Check Google Benchmark JSON output against optional thresholds."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any


def main() -> int:
  parser = argparse.ArgumentParser()
  parser.add_argument("--results", required=True, type=Path)
  parser.add_argument("--thresholds", required=True, type=Path)
  args = parser.parse_args()

  results = load_json(args.results)
  thresholds = load_json(args.thresholds)
  result_by_name = {
      benchmark["name"]: benchmark
      for benchmark in results.get("benchmarks", [])
  }

  failures: list[str] = []
  for name, limits in thresholds.get("benchmarks", {}).items():
    benchmark = result_by_name.get(name)
    if benchmark is None:
      failures.append(f"{name}: missing benchmark result")
      continue
    failures.extend(check_limit(name, benchmark, limits, "real_time"))
    failures.extend(check_limit(name, benchmark, limits, "cpu_time"))

  if failures:
    print("\n".join(failures), file=sys.stderr)
    return 1
  return 0


def load_json(path: Path) -> dict[str, Any]:
  with path.open(encoding="utf-8") as file:
    data = json.load(file)
  if not isinstance(data, dict):
    raise TypeError(f"{path} must contain a JSON object")
  return data


def check_limit(
    name: str,
    benchmark: dict[str, Any],
    limits: dict[str, Any],
    field: str,
) -> list[str]:
  limit = limits.get(f"max_{field}_ns")
  if limit is None:
    return []

  value = benchmark.get(field)
  unit = benchmark.get("time_unit")
  if value is None or unit is None:
    return [f"{name}: missing {field} or time_unit"]

  value_ns = to_nanoseconds(float(value), str(unit))
  if value_ns <= float(limit):
    return []
  return [f"{name}: {field} {value_ns:.3f} ns exceeds {limit} ns"]


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
