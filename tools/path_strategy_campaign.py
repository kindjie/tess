#!/usr/bin/env python3
"""Run and summarize the advisory path-strategy crossover campaign."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import random
import resource
import signal
import statistics
import subprocess
import sys
import tempfile
import time
from typing import Any


PRIMARY_COUNTS = (1, 2, 4, 8, 10, 16, 32, 64, 100, 128, 256, 512, 1000)
CAPACITY_COUNTS = (1000, 2048, 4096, 8192, 16384, 32768, 65536, 131072)
ALL_DISTINCT_CAPACITY_COUNTS = (1000, 2044)
CAPACITY_EXTENTS = (512, 1024, 2048, 4096, 8192, 16384)
PAIRS = (
  ("unit_shared/open", "astar", "distance_field"),
  ("unit_shared/room_portals", "astar", "distance_field"),
  ("route_cache/exact_repeats", "astar", "cold_route_cache"),
  ("route_cache/same_goal_suffixes", "astar", "cold_route_cache"),
  ("weighted/one_goal", "weighted_astar", "weighted_batch"),
  ("weighted/eight_goals", "weighted_astar", "weighted_batch"),
  ("weighted/all_distinct_goals", "weighted_astar", "weighted_batch"),
)


class CampaignError(RuntimeError):
  """A campaign input or child result was invalid."""


def sha256(path: Path) -> str:
  """Return a file's SHA-256 digest."""
  digest = hashlib.sha256()
  with path.open("rb") as stream:
    for block in iter(lambda: stream.read(1024 * 1024), b""):
      digest.update(block)
  return digest.hexdigest()


def exact_name(family: str, strategy: str, extent: int, count: int) -> str:
  """Construct one exact benchmark registration name."""
  return (
    f"lab/path_strategy_crossover/{family}/{strategy}/{extent}x{extent}/{count}"
  )


def percentile(values: list[float], fraction: float) -> float:
  """Return a linearly interpolated sample percentile."""
  ordered = sorted(values)
  if len(ordered) == 1:
    return ordered[0]
  position = (len(ordered) - 1) * fraction
  lower = math.floor(position)
  upper = math.ceil(position)
  if lower == upper:
    return ordered[lower]
  return ordered[lower] + (ordered[upper] - ordered[lower]) * (position - lower)


def summarize_samples(values: list[float]) -> dict[str, float]:
  """Calculate the campaign's descriptive statistics."""
  if not values:
    raise CampaignError("cannot summarize an empty sample")
  mean = statistics.fmean(values)
  deviation = statistics.stdev(values) if len(values) > 1 else 0.0
  return {
    "median_cpu_ns": statistics.median(values),
    "q1_cpu_ns": percentile(values, 0.25),
    "q3_cpu_ns": percentile(values, 0.75),
    "p95_cpu_ns": percentile(values, 0.95),
    "coefficient_of_variation": deviation / mean if mean else 0.0,
  }


def classify_paired_ratios(
  ratios: list[float], practical_effect: float
) -> dict[str, float | str]:
  """Classify a paired result only when its empirical interval clears noise."""
  lower = percentile(ratios, 0.05)
  upper = percentile(ratios, 0.95)
  if upper < 1.0 - practical_effect:
    verdict = "right_wins"
  elif lower > 1.0 + practical_effect:
    verdict = "left_wins"
  else:
    verdict = "inconclusive"
  return {
    "median_right_over_left": statistics.median(ratios),
    "p05_right_over_left": lower,
    "p95_right_over_left": upper,
    "verdict": verdict,
  }


def crossover_analysis(rows: list[dict[str, Any]]) -> dict[str, Any]:
  """Report a stable first right-arm win and all observed reversals."""
  ordered = sorted(rows, key=lambda item: item["count"])
  decisive = [
    row for row in ordered if row["paired"]["verdict"] != "inconclusive"
  ]
  transitions = []
  for previous, current in zip(decisive, decisive[1:]):
    if previous["paired"]["verdict"] != current["paired"]["verdict"]:
      transitions.append({
        "lower_count": previous["count"],
        "upper_count": current["count"],
        "from": previous["paired"]["verdict"],
        "to": current["paired"]["verdict"],
      })
  bracket = None
  for index, row in enumerate(decisive):
    if row["paired"]["verdict"] != "right_wins":
      continue
    if any(
      later["paired"]["verdict"] == "left_wins"
      for later in decisive[index + 1 :]
    ):
      continue
    lower = 0 if index == 0 else decisive[index - 1]["count"]
    bracket = {"lower_exclusive": lower, "upper_inclusive": row["count"]}
    break
  return {"stable_right_win_bracket": bracket, "transitions": transitions}


def stable_counters(counters: dict[str, Any]) -> dict[str, Any]:
  """Remove timing and process-peak values before determinism comparison."""
  return {
    key: value
    for key, value in counters.items()
    if key not in {"items_per_second", "process.peak_rss_bytes"}
  }


def validate_capacity_mechanism(
  family: str, strategy: str, count: int, observation: dict[str, Any]
) -> None:
  """Reject a completed weighted cell that did not exercise its named path."""
  if observation["status"] != "complete" or strategy != "weighted_batch":
    return
  counters = observation["counters"]
  fields = counters.get("batch.field_builds")
  fallbacks = counters.get("batch.astar_fallbacks")
  if family == "weighted/all_distinct_goals":
    valid = fields == 0 and fallbacks == count
  else:
    valid = fields is not None and fields > 0
  if not valid:
    raise CampaignError(
      f"capacity cell did not exercise the expected mechanism: {family}, "
      f"{strategy}, count={count}, fields={fields}, fallbacks={fallbacks}"
    )


def process_limits(limit_bytes: int, cpu: int | None):
  """Return Linux child limits; Darwin is enforced by the parent watchdog."""

  def apply_limit() -> None:
    if limit_bytes and sys.platform != "darwin":
      resource.setrlimit(resource.RLIMIT_AS, (limit_bytes, limit_bytes))
    if cpu is not None and hasattr(os, "sched_setaffinity"):
      os.sched_setaffinity(0, {cpu})

  return apply_limit


def child_rss_bytes(pid: int) -> int | None:
  """Read resident bytes for a child without adding a runtime dependency."""
  try:
    result = subprocess.run(
      ["ps", "-o", "rss=", "-p", str(pid)],
      check=False,
      capture_output=True,
      text=True,
      timeout=0.5,
    )
  except (OSError, subprocess.TimeoutExpired):
    return None
  try:
    return int(result.stdout.strip()) * 1024
  except ValueError:
    return None


def run_cell(
  binary: Path,
  name: str,
  timeout_seconds: float,
  memory_limit_bytes: int,
  minimum_time: float,
  cpu: int | None,
) -> dict[str, Any]:
  """Run one exact registration in an isolated, resource-bounded process."""
  command = [
    str(binary),
    f"--benchmark_filter=^{name}$",
    "--benchmark_format=json",
    f"--benchmark_min_time={minimum_time}s",
  ]
  started = time.monotonic()
  try:
    process = subprocess.Popen(
      command,
      stdout=subprocess.PIPE,
      stderr=subprocess.PIPE,
      text=True,
      preexec_fn=(
        process_limits(memory_limit_bytes, cpu) if os.name == "posix" else None
      ),
      start_new_session=True,
    )
  except (OSError, ValueError, subprocess.SubprocessError) as error:
    return {
      "name": name,
      "status": "failed",
      "wall_seconds": time.monotonic() - started,
      "detail": str(error),
    }
  status = "complete"
  watchdog_peak = 0
  try:
    while process.poll() is None:
      elapsed = time.monotonic() - started
      rss = child_rss_bytes(process.pid)
      watchdog_peak = max(watchdog_peak, rss or 0)
      if sys.platform == "darwin" and rss is None:
        status = "resource_monitor_unavailable"
        os.killpg(process.pid, signal.SIGKILL)
        break
      if elapsed > timeout_seconds:
        status = "timeout"
        os.killpg(process.pid, signal.SIGKILL)
        break
      if (
        sys.platform == "darwin"
        and rss is not None
        and rss > memory_limit_bytes
      ):
        status = "memory_limit"
        os.killpg(process.pid, signal.SIGKILL)
        break
      time.sleep(0.02)
    stdout, stderr = process.communicate()
  except BaseException:
    if process.poll() is None:
      os.killpg(process.pid, signal.SIGKILL)
    process.wait()
    raise
  elapsed = time.monotonic() - started
  if status != "complete" or process.returncode != 0:
    return {
      "name": name,
      "status": status if status != "complete" else "failed",
      "returncode": process.returncode,
      "wall_seconds": elapsed,
      "watchdog_peak_rss_bytes": watchdog_peak,
      "detail": stderr[-2000:],
    }
  try:
    payload = json.loads(stdout)
    rows = [
      row
      for row in payload["benchmarks"]
      if row.get("run_type", "iteration") == "iteration"
    ]
  except (KeyError, TypeError, json.JSONDecodeError) as error:
    raise CampaignError(
      f"invalid benchmark JSON for {name}: {error}"
    ) from error
  if len(rows) != 1 or rows[0].get("name") != name:
    raise CampaignError(f"exact filter for {name} returned {len(rows)} rows")
  row = rows[0]
  counters = {
    key: value
    for key, value in row.items()
    if key
    not in {
      "name",
      "family_index",
      "per_family_instance_index",
      "run_name",
      "run_type",
      "repetitions",
      "repetition_index",
      "threads",
      "iterations",
      "real_time",
      "cpu_time",
      "time_unit",
    }
    and isinstance(value, (int, float))
  }
  scale = {"ns": 1.0, "us": 1000.0, "ms": 1_000_000.0}.get(row.get("time_unit"))
  if scale is None:
    raise CampaignError(f"unsupported time unit for {name}")
  return {
    "name": name,
    "status": "complete",
    "cpu_ns": row["cpu_time"] * scale,
    "wall_seconds": elapsed,
    "watchdog_peak_rss_bytes": watchdog_peak,
    "iterations": row["iterations"],
    "counters": counters,
  }


def atomic_write(path: Path, payload: dict[str, Any]) -> None:
  """Checkpoint JSON through an atomic same-directory rename."""
  path.parent.mkdir(parents=True, exist_ok=True)
  with tempfile.NamedTemporaryFile(
    "w", encoding="utf-8", dir=path.parent, delete=False
  ) as stream:
    json.dump(payload, stream, indent=2, sort_keys=True)
    stream.write("\n")
    temporary = Path(stream.name)
  temporary.replace(path)


def base_payload(args: argparse.Namespace) -> dict[str, Any]:
  """Create auditable campaign metadata shared by both modes."""
  environment = json.loads(args.environment.read_text(encoding="utf-8"))
  required_environment = {
    "platform",
    "memory_bytes",
    "operating_system",
    "compiler",
    "cmake",
    "build_config",
    "source_commit",
    "affinity",
    "power",
    "notes",
  }
  if not isinstance(environment, dict):
    raise CampaignError("environment metadata must be a JSON object")
  missing = sorted(required_environment - environment.keys())
  if missing:
    raise CampaignError(
      f"environment metadata is missing: {', '.join(missing)}"
    )
  if not isinstance(environment["memory_bytes"], int):
    raise CampaignError("environment memory_bytes must be an integer")
  return {
    "schema": 1,
    "mode": args.command,
    "binary_sha256": sha256(args.binary),
    "source_sha256": sha256(args.source),
    "runner_sha256": sha256(Path(__file__)),
    "environment": environment,
    "policy": {
      "timeout_seconds": args.timeout,
      "memory_limit_bytes": args.memory_limit_gib * 1024**3,
      "minimum_time_seconds": args.minimum_time,
      "cpu": args.cpu,
      "resource_limit_kind": (
        "sampled_rss_watchdog_20ms" if sys.platform == "darwin" else "rlimit_as"
      ),
    },
    "observations": [],
  }


def run_primary(args: argparse.Namespace) -> dict[str, Any]:
  """Run randomized, paired repetitions for crossover inference."""
  payload = base_payload(args)
  payload["policy"].update({
    "repetitions": args.repetitions,
    "random_seed": args.seed,
    "maximum_cv": args.maximum_cv,
    "practical_effect": args.practical_effect,
  })
  rng = random.Random(args.seed)
  for family, left, right in PAIRS:
    for count in PRIMARY_COUNTS:
      names = [
        exact_name(family, left, 512, count),
        exact_name(family, right, 512, count),
      ]
      samples = {names[0]: [], names[1]: []}
      repetitions = []
      counters: dict[str, dict[str, Any]] = {}
      peak_rss = {names[0]: 0, names[1]: 0}
      for repetition in range(args.repetitions):
        order = names.copy()
        rng.shuffle(order)
        paired_observations = {}
        for name in order:
          observation = run_cell(
            args.binary,
            name,
            args.timeout,
            args.memory_limit_gib * 1024**3,
            args.minimum_time,
            args.cpu,
          )
          if observation["status"] != "complete":
            raise CampaignError(f"primary cell did not complete: {observation}")
          samples[name].append(observation["cpu_ns"])
          observed_counters = stable_counters(observation["counters"])
          if name in counters and counters[name] != observed_counters:
            raise CampaignError(f"deterministic counters changed for {name}")
          counters[name] = observed_counters
          peak_rss[name] = max(
            peak_rss[name],
            observation["watchdog_peak_rss_bytes"],
            int(observation["counters"].get("process.peak_rss_bytes", 0)),
          )
          paired_observations[name] = {
            "cpu_ns": observation["cpu_ns"],
            "iterations": observation["iterations"],
            "watchdog_peak_rss_bytes": observation["watchdog_peak_rss_bytes"],
            "reported_peak_rss_bytes": observation["counters"].get(
              "process.peak_rss_bytes"
            ),
          }
        repetitions.append({
          "index": repetition,
          "order": order,
          "observations": paired_observations,
        })
      ratios = [
        samples[names[1]][index] / samples[names[0]][index]
        for index in range(args.repetitions)
      ]
      row = {
        "family": family,
        "extent": 512,
        "count": count,
        "left": {"strategy": left, **summarize_samples(samples[names[0]])},
        "right": {"strategy": right, **summarize_samples(samples[names[1]])},
        "paired": classify_paired_ratios(ratios, args.practical_effect),
        "counters": counters,
        "peak_rss_bytes": peak_rss,
        "repetitions": repetitions,
      }
      row["accepted"] = (
        row["left"]["coefficient_of_variation"] <= args.maximum_cv
        and row["right"]["coefficient_of_variation"] <= args.maximum_cv
      )
      payload["observations"].append(row)
      atomic_write(args.output, payload)
  payload["crossovers"] = {}
  for family, _, _ in PAIRS:
    rows = [
      row
      for row in payload["observations"]
      if row["family"] == family and row["accepted"]
    ]
    payload["crossovers"][family] = crossover_analysis(rows)
  atomic_write(args.output, payload)
  return payload


def run_capacity(args: argparse.Namespace) -> dict[str, Any]:
  """Run ascending resource-bounded ladders and stop at first failure."""
  payload = base_payload(args)
  axes = []
  for family, left, right in PAIRS:
    for strategy in (left, right):
      counts = CAPACITY_COUNTS
      if family == "weighted/all_distinct_goals":
        counts = ALL_DISTINCT_CAPACITY_COUNTS
      axes.append((family, strategy, "requests", 512, counts))
      grid_count = 1
      if family == "weighted/one_goal":
        grid_count = 2
      elif family == "weighted/eight_goals":
        grid_count = 16
      axes.append((family, strategy, "grid", grid_count, CAPACITY_EXTENTS))
  for family, strategy, axis, fixed, rungs in axes:
    for rung in rungs:
      extent = fixed if axis == "requests" else rung
      count = rung if axis == "requests" else fixed
      name = exact_name(family, strategy, extent, count)
      observation = run_cell(
        args.binary,
        name,
        args.timeout,
        args.memory_limit_gib * 1024**3,
        args.minimum_time,
        args.cpu,
      )
      validate_capacity_mechanism(family, strategy, count, observation)
      observation.update({
        "family": family,
        "strategy": strategy,
        "axis": axis,
        "extent": extent,
        "count": count,
      })
      payload["observations"].append(observation)
      atomic_write(args.output, payload)
      if observation["status"] != "complete":
        break
    if (
      family == "weighted/all_distinct_goals"
      and axis == "requests"
      and observation["status"] == "complete"
      and rung == 2044
    ):
      payload["observations"].append({
        "family": family,
        "strategy": strategy,
        "axis": axis,
        "extent": 512,
        "count": 2045,
        "status": "fixture_limited",
        "fixture_max_count": 2044,
      })
      atomic_write(args.output, payload)
  return payload


def parser() -> argparse.ArgumentParser:
  """Build the command-line parser."""
  result = argparse.ArgumentParser(description=__doc__)
  common = argparse.ArgumentParser(add_help=False)
  common.add_argument("--binary", type=Path, required=True)
  common.add_argument("--source", type=Path, required=True)
  common.add_argument("--environment", type=Path, required=True)
  common.add_argument("--output", type=Path, required=True)
  common.add_argument("--timeout", type=float, default=60.0)
  common.add_argument("--memory-limit-gib", type=int, required=True)
  common.add_argument("--minimum-time", type=float, default=0.01)
  common.add_argument("--cpu", type=int)
  subparsers = result.add_subparsers(dest="command", required=True)
  primary = subparsers.add_parser("primary", parents=[common])
  primary.add_argument("--repetitions", type=int, default=10)
  primary.add_argument("--seed", type=int, default=20260828)
  primary.add_argument("--maximum-cv", type=float, default=0.05)
  primary.add_argument("--practical-effect", type=float, default=0.02)
  subparsers.add_parser("capacity", parents=[common])
  return result


def main() -> int:
  """Run the requested campaign."""
  args = parser().parse_args()
  if not args.binary.is_file() or not args.source.is_file():
    raise CampaignError("binary and source must be regular files")
  if args.memory_limit_gib <= 0:
    raise CampaignError("memory limit must be greater than zero")
  if args.timeout <= 0 or args.minimum_time <= 0:
    raise CampaignError("timeout and minimum time must be greater than zero")
  if args.cpu is not None and args.cpu < 0:
    raise CampaignError("CPU must be non-negative")
  if args.command == "primary" and args.repetitions < 10:
    raise CampaignError("primary evidence requires at least 10 repetitions")
  if args.command == "primary" and not 0 < args.maximum_cv < 1:
    raise CampaignError("maximum CV must be between zero and one")
  if args.command == "primary" and not 0 < args.practical_effect < 1:
    raise CampaignError("practical effect must be between zero and one")
  environment = json.loads(args.environment.read_text(encoding="utf-8"))
  if (
    isinstance(environment, dict)
    and isinstance(environment.get("memory_bytes"), int)
    and args.memory_limit_gib * 1024**3 >= environment["memory_bytes"]
  ):
    raise CampaignError("memory limit must be below reported physical memory")
  payload = (
    run_primary(args) if args.command == "primary" else run_capacity(args)
  )
  print(
    json.dumps({
      "output": str(args.output),
      "cells": len(payload["observations"]),
    })
  )
  return 0


if __name__ == "__main__":
  try:
    sys.exit(main())
  except CampaignError as error:
    print(f"error: {error}", file=sys.stderr)
    sys.exit(2)
