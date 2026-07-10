#!/usr/bin/env python3
"""Check Google Benchmark JSON output against optional thresholds."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any


class ToolError(Exception):
  """Input error that should be reported without a traceback."""


# Keys permitted in a per-benchmark thresholds entry: the two gate limits
# plus the annotation keys used across bench/thresholds/*.json. A typo in
# a limit key (for example `max_cpu_tim_ns`) must fail loudly instead of
# silently disabling the gate.
ALLOWED_LIMIT_KEYS = frozenset(
    {
        "max_real_time_ns",
        "max_cpu_time_ns",
        "comment",
        "comment_ref",
        "note",
    }
)


def main(argv: list[str] | None = None) -> int:
  parser = argparse.ArgumentParser()
  parser.add_argument("--results", required=True, type=Path)
  parser.add_argument("--thresholds", required=True, type=Path)
  parser.add_argument(
      "--aggregate",
      default="median",
      help=(
          "Aggregate to gate on when --benchmark_repetitions output is "
          "present (default: median)."
      ),
  )
  args = parser.parse_args(argv)

  try:
    results = load_json(args.results)
    thresholds = load_json(args.thresholds)
    result_by_name = select_benchmarks(
        results.get("benchmarks", []), args.aggregate
    )
  except ToolError as error:
    print(f"benchmark_thresholds: {error}", file=sys.stderr)
    return 1

  failures: list[str] = []
  for name, limits in thresholds.get("benchmarks", {}).items():
    unknown = check_limit_keys(name, limits)
    if unknown:
      failures.extend(unknown)
      continue
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


def base_name(benchmark: dict[str, Any]) -> str:
  # Repetition entries and their aggregates share `run_name`; aggregate
  # entries suffix `name` (for example `_median`), so `run_name` is the
  # stable threshold key.
  name = benchmark.get("run_name", benchmark.get("name"))
  if not isinstance(name, str):
    raise ToolError("benchmark entry without a usable name")
  return name


def select_benchmarks(
    benchmarks: list[dict[str, Any]], aggregate: str
) -> dict[str, dict[str, Any]]:
  grouped: dict[str, list[dict[str, Any]]] = {}
  for benchmark in benchmarks:
    grouped.setdefault(base_name(benchmark), []).append(benchmark)

  selected: dict[str, dict[str, Any]] = {}
  for name, entries in grouped.items():
    aggregates = [
        entry for entry in entries if entry.get("run_type") == "aggregate"
    ]
    if aggregates:
      matches = [
          entry
          for entry in aggregates
          if entry.get("aggregate_name") == aggregate
      ]
      if not matches:
        raise ToolError(
            f"{name}: repetition output has no '{aggregate}' aggregate"
        )
      if len(matches) > 1:
        raise ToolError(f"{name}: duplicate '{aggregate}' aggregate entries")
      selected[name] = matches[0]
      continue
    if len(entries) > 1:
      raise ToolError(
          f"{name}: duplicate benchmark entries without aggregates; "
          "results are ambiguous"
      )
    selected[name] = entries[0]
  return selected


def check_limit_keys(name: str, limits: dict[str, Any]) -> list[str]:
  unknown = sorted(set(limits) - ALLOWED_LIMIT_KEYS)
  return [
      f"{name}: unknown limit key '{key}' "
      f"(allowed: {', '.join(sorted(ALLOWED_LIMIT_KEYS))})"
      for key in unknown
  ]


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
