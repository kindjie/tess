#!/usr/bin/env python3
"""Check Google Benchmark JSON output against optional thresholds."""

from __future__ import annotations

import argparse
import json
import math
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
        "gating",
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
  parser.add_argument(
      "--allow-missing-result",
      action="append",
      default=[],
      help=(
          "Threshold name expected to be absent because its optional "
          "benchmark provider is disabled; repeat for multiple names."
      ),
  )
  args = parser.parse_args(argv)

  try:
    results = load_json(args.results)
    thresholds = load_json(args.thresholds)
    result_entries = results.get("benchmarks")
    if not isinstance(result_entries, list):
      raise ToolError("results 'benchmarks' must be a JSON array")
    if not result_entries:
      raise ToolError("no benchmark results")
    threshold_by_name = thresholds.get("benchmarks")
    if not isinstance(threshold_by_name, dict):
      raise ToolError("thresholds 'benchmarks' must be a JSON object")
    result_by_name = select_benchmarks(result_entries, args.aggregate)
  except ToolError as error:
    print(f"benchmark_thresholds: {error}", file=sys.stderr)
    return 1

  failures: list[str] = []
  result_names = set(result_by_name)
  threshold_names = set(threshold_by_name)
  allowed_missing = set(args.allow_missing_result)
  failures.extend(
      f"{name}: unknown allowed-missing threshold"
      for name in sorted(allowed_missing - threshold_names)
  )
  failures.extend(
      f"{name}: missing benchmark result"
      for name in sorted(threshold_names - result_names - allowed_missing)
  )
  failures.extend(
      f"{name}: missing threshold entry"
      for name in sorted(result_names - threshold_names)
  )

  valid_thresholds: set[str] = set()
  for name in sorted(threshold_names):
    limits = threshold_by_name[name]
    if not isinstance(limits, dict):
      failures.append(f"{name}: threshold entry must be a JSON object")
      continue
    entry_failures = validate_limit_entry(name, limits)
    failures.extend(entry_failures)
    if not entry_failures:
      valid_thresholds.add(name)

  for name in sorted(result_names & threshold_names):
    limits = threshold_by_name[name]
    if name not in valid_thresholds:
      continue
    if limits.get("gating") is False:
      continue
    benchmark = result_by_name[name]
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


def load_threshold_metrics(thresholds_dir: Path) -> dict[str, str]:
  """Map benchmark names to the metric their family is judged on.

  A benchmark with a real-time ceiling (the parallel pool suite and the
  manually timed cache benchmarks) is judged on real time everywhere it
  is analysed, not only by the gate: the parallel families set
  `max_cpu_time_ns` to null on purpose, because pool work happens on
  worker threads and the dispatching thread's CPU time understates the
  operation. Everything else, including ungated lab registrations with
  no manifest entry at all, defaults to CPU time.

  Shared by the gate's siblings — the paired sentinel confirmation, the
  change-point detector and the trend renderer — so an alert, its
  confirmation command and the published series cannot disagree about
  which number they mean.
  """
  metrics: dict[str, str] = {}
  for manifest in sorted(thresholds_dir.glob("*.json")):
    entries = load_json(manifest)
    for name, entry in entries.get("benchmarks", {}).items():
      if not isinstance(entry, dict):
        continue
      if entry.get("max_real_time_ns") is not None:
        metrics[str(name)] = "real_time"
      else:
        metrics[str(name)] = "cpu_time"
  return metrics


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


def validate_limit_entry(
  name: str, limits: dict[str, Any]
) -> list[str]:
  failures = check_limit_keys(name, limits)
  if failures:
    return failures
  gating = limits.get("gating", True)
  if not isinstance(gating, bool):
    return [f"{name}: gating must be true or false"]
  enabled_limits = [
    key
    for key in ("max_real_time_ns", "max_cpu_time_ns")
    if limits.get(key) is not None
  ]
  if not gating:
    if enabled_limits:
      return [
        f"{name}: gating=false conflicts with enabled time limits"
      ]
    return []
  if not enabled_limits:
    return [
      f"{name}: no enabled time limit; set gating=false for an "
      "intentional informational entry"
    ]
  return []


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

  try:
    value_number = float(value)
    limit_number = float(limit)
  except (OverflowError, TypeError, ValueError):
    return [f"{name}: {field} and max_{field}_ns must be numeric"]
  if not math.isfinite(value_number) or value_number < 0:
    return [f"{name}: {field} must be a finite non-negative number"]
  if not math.isfinite(limit_number) or limit_number < 0:
    return [f"{name}: max_{field}_ns must be a finite non-negative number"]
  try:
    value_ns = to_nanoseconds(value_number, str(unit))
  except ValueError:
    return [f"{name}: unsupported time_unit {unit!r}"]
  if value_ns <= limit_number:
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
