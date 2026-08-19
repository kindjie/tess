#!/usr/bin/env python3
"""Collect advisory Traffic Lab fixed-tick latency percentiles."""

from __future__ import annotations

import argparse
import csv
from datetime import datetime, timezone
import hashlib
import io
import json
import math
from pathlib import Path
import platform
import subprocess
import time


SCENARIOS = ("aligned", "shuffled-crossing", "funnel", "multi-gate")
PERCENTILE_MINIMUMS = {"p50": 20, "p95": 200, "p99": 2000}
INTEGER_FIELDS = (
  "tick",
  "planning_queries",
  "waits",
  "blocked",
  "arrived",
  "pending",
  "advanced",
)
OPTIONAL_INTEGER_FIELDS = (
  "touched_nodes",
  "heap_pops",
  "neighbor_candidates",
  "passability_checks",
  "reconstructed_nodes",
)
FLOAT_FIELDS = ("update_us", "planning_us")


def nearest_rank(sorted_values: list[float], quantile: float) -> float:
  """Return a nearest-rank percentile from a non-empty sorted sequence."""
  index = max(0, math.ceil(quantile * len(sorted_values)) - 1)
  return sorted_values[index]


def summarize(values: list[float]) -> dict[str, float | int | None]:
  """Summarize samples while suppressing unsupported tail percentiles."""
  if not values:
    raise ValueError("cannot summarize an empty sample family")
  ordered = sorted(values)
  enough_for_quartiles = len(ordered) >= PERCENTILE_MINIMUMS["p50"]
  return {
    "samples": len(ordered),
    "minimum": ordered[0],
    "p25": nearest_rank(ordered, 0.25) if enough_for_quartiles else None,
    "p50": (
      nearest_rank(ordered, 0.50)
      if len(ordered) >= PERCENTILE_MINIMUMS["p50"]
      else None
    ),
    "p75": nearest_rank(ordered, 0.75) if enough_for_quartiles else None,
    "p95": (
      nearest_rank(ordered, 0.95)
      if len(ordered) >= PERCENTILE_MINIMUMS["p95"]
      else None
    ),
    "p99": (
      nearest_rank(ordered, 0.99)
      if len(ordered) >= PERCENTILE_MINIMUMS["p99"]
      else None
    ),
    "maximum": ordered[-1],
  }


def parse_samples(
  output: str, scenario: str, expected_ticks: int
) -> list[dict[str, float | int | str]]:
  """Parse and validate one native fixed-tick CSV stream."""
  rows: list[dict[str, float | int | str]] = []
  for expected_tick, source in enumerate(csv.DictReader(io.StringIO(output))):
    if source.get("scenario") != scenario:
      raise ValueError(
        f"expected scenario {scenario!r}, got {source.get('scenario')!r}"
      )
    row: dict[str, float | int | str] = {"scenario": scenario}
    try:
      for field in INTEGER_FIELDS:
        row[field] = int(source[field])
      for field in OPTIONAL_INTEGER_FIELDS:
        if source.get(field) is not None:
          row[field] = int(source[field])
      for field in FLOAT_FIELDS:
        row[field] = float(source[field])
    except (KeyError, TypeError, ValueError) as error:
      raise ValueError(f"malformed native sample row: {source}") from error
    if row["tick"] != expected_tick:
      raise ValueError(
        f"expected tick {expected_tick}, got {row['tick']}"
      )
    if row["planning_queries"] > 8:
      raise ValueError("native sample exceeded the eight-search budget")
    rows.append(row)
  if len(rows) != expected_ticks:
    raise ValueError(
      f"expected {expected_ticks} samples, received {len(rows)}"
    )
  return rows


def run_repetition(
  binary: Path, scenario: str, ticks: int
) -> list[dict[str, float | int | str]]:
  """Run one scenario in its own process and return validated samples."""
  command = [
    str(binary),
    "--scenario",
    scenario,
    "--ticks",
    str(ticks),
    "--samples",
  ]
  result = subprocess.run(
    command,
    check=True,
    capture_output=True,
    text=True,
  )
  return parse_samples(result.stdout, scenario, ticks)


def validate_pass(
  samples: list[dict[str, object]], counter_pass: bool
) -> None:
  """Reject instrumented timing or a counter pass without instrumentation."""
  observed = sum(int(sample.get("passability_checks", 0))
                 for sample in samples)
  if counter_pass and observed == 0:
    raise ValueError(
      "counter pass observed no path instrumentation; use the diagnostics "
      "binary"
    )
  if not counter_pass and observed != 0:
    raise ValueError(
      "timing pass observed diagnostic counters; use the uninstrumented "
      "binary"
    )


def summarize_counters(
  samples: list[dict[str, object]], field: str
) -> dict[str, int]:
  """Summarize deterministic work without publishing instrumented timing."""
  values = [int(sample.get(field, 0)) for sample in samples]
  return {"total": sum(values), "maximum_per_tick": max(values)}


def file_sha256(path: Path) -> str:
  """Hash the measured executable without loading it all at once."""
  digest = hashlib.sha256()
  with path.open("rb") as source:
    while block := source.read(1024 * 1024):
      digest.update(block)
  return digest.hexdigest()


def collect(
  binary: Path,
  scenarios: list[str],
  ticks: int,
  repetitions: int,
  counter_pass: bool = False,
) -> dict[str, object]:
  """Collect all requested scenarios and return one advisory artifact."""
  started = time.monotonic()
  results = []
  for scenario in scenarios:
    raw_samples = []
    for repetition in range(repetitions):
      samples = run_repetition(binary, scenario, ticks)
      for sample in samples:
        sample["repetition"] = repetition
      raw_samples.extend(samples)
    validate_pass(raw_samples, counter_pass)
    result: dict[str, object] = {
      "scenario": scenario,
      "raw_samples": raw_samples,
    }
    if counter_pass:
      result["counters"] = {
        field: summarize_counters(raw_samples, field)
        for field in OPTIONAL_INTEGER_FIELDS
      }
    else:
      result["update_us"] = summarize(
        [float(sample["update_us"]) for sample in raw_samples]
      )
      result["planning_us"] = summarize(
        [float(sample["planning_us"]) for sample in raw_samples]
      )
    results.append(result)
  return {
    "schema_version": 1,
    "authority": "advisory",
    "generated_at": datetime.now(timezone.utc).isoformat(),
    "environment": {
      "platform": platform.platform(),
      "machine": platform.machine(),
      "python": platform.python_version(),
    },
    "method": {
      "binary": str(binary.resolve()),
      "binary_sha256": file_sha256(binary),
      "ticks_per_process": ticks,
      "fresh_process_repetitions": repetitions,
      "pass": "counter" if counter_pass else "timing",
      "percentile_method": "nearest-rank",
      "percentile_minimums": PERCENTILE_MINIMUMS,
      "elapsed_seconds": time.monotonic() - started,
    },
    "scenarios": results,
  }


def format_value(value: object) -> str:
  """Format one summary cell, including a suppressed percentile."""
  return "insufficient" if value is None else f"{float(value):.2f}"


def print_summary(artifact: dict[str, object]) -> None:
  """Print a concise Markdown summary for a captured artifact."""
  if artifact["method"]["pass"] == "counter":
    print("| scenario | touched nodes | heap pops | neighbor candidates |")
    print("| --- | ---: | ---: | ---: |")
    for result in artifact["scenarios"]:
      counters = result["counters"]
      print(
        f"| {result['scenario']} | {counters['touched_nodes']['total']} | "
        f"{counters['heap_pops']['total']} | "
        f"{counters['neighbor_candidates']['total']} |"
      )
    return
  print("| scenario | samples | update p50/p95/p99 us | planning p50/p95/p99 us |")
  print("| --- | ---: | ---: | ---: |")
  for result in artifact["scenarios"]:
    update = result["update_us"]
    planning = result["planning_us"]
    update_text = "/".join(format_value(update[key]) for key in
                           ("p50", "p95", "p99"))
    planning_text = "/".join(format_value(planning[key]) for key in
                             ("p50", "p95", "p99"))
    print(
      f"| {result['scenario']} | {update['samples']} | {update_text} | "
      f"{planning_text} |"
    )


def main() -> int:
  """Parse arguments, run the campaign, and write its advisory artifact."""
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("--binary", type=Path, required=True)
  parser.add_argument("--scenario", action="append", choices=SCENARIOS)
  parser.add_argument("--ticks", type=int, default=128)
  parser.add_argument("--repetitions", type=int, default=16)
  parser.add_argument("--counter-pass", action="store_true")
  parser.add_argument("--output", type=Path, required=True)
  args = parser.parse_args()
  if args.ticks <= 0 or args.repetitions <= 0:
    parser.error("--ticks and --repetitions must be positive")
  if not args.binary.is_file():
    parser.error(f"binary does not exist: {args.binary}")

  artifact = collect(
    args.binary,
    args.scenario or list(SCENARIOS),
    args.ticks,
    args.repetitions,
    args.counter_pass,
  )
  args.output.parent.mkdir(parents=True, exist_ok=True)
  args.output.write_text(json.dumps(artifact, indent=2) + "\n")
  print_summary(artifact)
  print(f"artifact: {args.output}")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
