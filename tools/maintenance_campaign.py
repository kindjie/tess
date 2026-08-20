#!/usr/bin/env python3
"""Collect and analyze paired maintenance-adapter promotion evidence."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import platform
import random
import re
import statistics
import subprocess
import sys
from collections.abc import Mapping, Sequence
from pathlib import Path
from typing import Any


class ToolError(RuntimeError):
  """A malformed input or failed campaign command."""


def load_config(path: Path) -> dict[str, Any]:
  """Load and shape-check the frozen campaign policy."""
  try:
    data = json.loads(path.read_text(encoding="utf-8"))
  except (OSError, json.JSONDecodeError) as error:
    raise ToolError(f"cannot load {path}: {error}") from error
  if not isinstance(data, dict) or data.get("schema_version") != 1:
    raise ToolError(f"{path}: unsupported campaign schema")
  backends = data.get("backends")
  controls = data.get("controls")
  workloads = data.get("workloads")
  collection = data.get("collection")
  analysis = data.get("analysis")
  if not isinstance(backends, list) or "dirty_bit" not in backends:
    raise ToolError(f"{path}: backends must include dirty_bit")
  if not isinstance(controls, list) or not controls:
    raise ToolError(f"{path}: controls must be a nonempty list")
  if not isinstance(workloads, dict) or not workloads:
    raise ToolError(f"{path}: workloads must be a nonempty object")
  if not isinstance(collection, dict):
    raise ToolError(f"{path}: collection must be an object")
  if not isinstance(analysis, dict):
    raise ToolError(f"{path}: analysis must be an object")
  expected = set(backends)
  for workload, entry in workloads.items():
    benchmarks = entry.get("benchmarks") if isinstance(entry, dict) else None
    if not isinstance(benchmarks, dict) or set(benchmarks) != expected:
      raise ToolError(
        f"{path}: workload {workload!r} must map every backend exactly"
      )
  if set(controls) != expected - {"dirty_bit"}:
    raise ToolError(f"{path}: controls must be every non-dirty backend")
  calibration_backend = analysis.get("calibration_backend")
  primary_control = analysis.get("primary_control")
  primary_workloads = analysis.get("primary_workloads")
  guardrail_controls = analysis.get("guardrail_controls")
  if calibration_backend not in controls or primary_control not in controls:
    raise ToolError(
      f"{path}: calibration and primary controls must be controls"
    )
  if (
    not isinstance(primary_workloads, list)
    or not primary_workloads
    or not set(primary_workloads) <= set(workloads)
  ):
    raise ToolError(f"{path}: primary workloads must name configured workloads")
  if (
    not isinstance(guardrail_controls, list)
    or not guardrail_controls
    or not set(guardrail_controls) <= set(controls)
  ):
    raise ToolError(f"{path}: guardrail controls must name configured controls")
  repetitions = collection.get("repetitions")
  minimum_time = collection.get("minimum_time_seconds")
  devices = collection.get("devices")
  if (
    not isinstance(repetitions, int)
    or isinstance(repetitions, bool)
    or repetitions < 4
    or repetitions % 2 != 0
  ):
    raise ToolError(
      f"{path}: repetitions must be an even integer at least four"
    )
  if (
    not isinstance(minimum_time, (int, float))
    or isinstance(minimum_time, bool)
    or not math.isfinite(float(minimum_time))
    or minimum_time <= 0
  ):
    raise ToolError(f"{path}: minimum time must be positive and finite")
  if not isinstance(devices, dict) or len(devices) != 2:
    raise ToolError(f"{path}: collection must declare exactly two devices")
  for device, seeds in devices.items():
    if not isinstance(device, str) or not device or not isinstance(seeds, dict):
      raise ToolError(f"{path}: each collection device must declare seeds")
    if set(seeds) != {"calibration_seed", "candidate_seed"}:
      raise ToolError(f"{path}: {device!r} must declare both exact seeds")
    if any(
      not isinstance(seed, int) or isinstance(seed, bool)
      for seed in seeds.values()
    ):
      raise ToolError(f"{path}: {device!r} seeds must be integers")
  numeric_policy = {
    "confidence": analysis.get("confidence"),
    "minimum_relative_effect": analysis.get("minimum_relative_effect"),
    "minimum_absolute_effect_ns": analysis.get("minimum_absolute_effect_ns"),
    "noise_multiplier": analysis.get("noise_multiplier"),
    "maximum_relative_noise": analysis.get("maximum_relative_noise"),
  }
  if any(
    not isinstance(value, (int, float))
    or isinstance(value, bool)
    or not math.isfinite(float(value))
    or value <= 0
    for value in numeric_policy.values()
  ):
    raise ToolError(f"{path}: analysis thresholds must be positive and finite")
  if not 0 < float(numeric_policy["confidence"]) < 1:
    raise ToolError(f"{path}: confidence must be between zero and one")
  bootstrap_resamples = analysis.get("bootstrap_resamples")
  bootstrap_seed = analysis.get("bootstrap_seed")
  if (
    not isinstance(bootstrap_resamples, int)
    or isinstance(bootstrap_resamples, bool)
    or bootstrap_resamples < 1
    or not isinstance(bootstrap_seed, int)
    or isinstance(bootstrap_seed, bool)
  ):
    raise ToolError(f"{path}: bootstrap parameters must be integers")
  return data


def validate_collection_parameters(
  config: Mapping[str, Any],
  *,
  device: str,
  phase: str,
  repetitions: int,
  minimum_time_seconds: float,
  seed: int,
) -> None:
  """Reject any collection invocation outside the frozen device policy."""
  collection = config["collection"]
  device_policy = collection["devices"].get(device)
  if device_policy is None:
    raise ToolError(f"device {device!r} is not in the frozen campaign")
  if repetitions != collection["repetitions"]:
    raise ToolError("repetitions differ from the frozen campaign")
  if minimum_time_seconds != collection["minimum_time_seconds"]:
    raise ToolError("minimum time differs from the frozen campaign")
  seed_key = f"{phase}_seed"
  if seed != device_policy[seed_key]:
    raise ToolError(f"{phase} seed differs from the frozen campaign")


def _sha256(path: Path) -> str:
  digest = hashlib.sha256()
  with path.open("rb") as source:
    for block in iter(lambda: source.read(1024 * 1024), b""):
      digest.update(block)
  return digest.hexdigest()


def mapping_sha256(value: Mapping[str, Any]) -> str:
  """Hash a JSON mapping independently of whitespace and key order."""
  return hashlib.sha256(
    json.dumps(value, separators=(",", ":"), sort_keys=True).encode("utf-8")
  ).hexdigest()


def _cpu_model() -> str:
  if platform.system() == "Darwin":
    result = subprocess.run(
      ("sysctl", "-n", "machdep.cpu.brand_string"),
      capture_output=True,
      text=True,
      check=False,
    )
    if result.returncode == 0 and result.stdout.strip():
      return result.stdout.strip()
    return platform.processor() or "unknown"
  try:
    for line in Path("/proc/cpuinfo").read_text(encoding="utf-8").splitlines():
      if line.lower().startswith("model name"):
        return line.split(":", 1)[1].strip()
  except OSError:
    pass
  return platform.processor() or "unknown"


def _environment() -> dict[str, Any]:
  return {
    "system": platform.system(),
    "release": platform.release(),
    "machine": platform.machine(),
    "cpu_model": _cpu_model(),
    "logical_cpus": os.cpu_count(),
  }


def _toolchain(compiler: Path) -> dict[str, str]:
  compiler = compiler.resolve()
  if not compiler.is_file():
    raise ToolError(f"compiler executable does not exist: {compiler}")
  result = subprocess.run(
    (str(compiler), "--version"),
    capture_output=True,
    text=True,
    check=False,
    timeout=30,
  )
  if result.returncode != 0 or not result.stdout.strip():
    raise ToolError(f"cannot identify compiler executable: {compiler}")
  version = "\n".join(
    line
    for line in result.stdout.strip().splitlines()
    if not line.startswith("InstalledDir:")
  )
  if not version:
    raise ToolError(f"compiler emitted no portable version: {compiler}")
  return {
    "name": compiler.name,
    "sha256": _sha256(compiler),
    "version": version,
  }


def _validate_toolchain(value: Any) -> None:
  if not isinstance(value, dict) or set(value) != {
    "name",
    "sha256",
    "version",
  }:
    raise ToolError("campaign has an invalid toolchain identity")
  if (
    not isinstance(value["name"], str)
    or not value["name"]
    or not isinstance(value["version"], str)
    or not value["version"]
    or not isinstance(value["sha256"], str)
    or re.fullmatch(r"[0-9a-f]{64}", value["sha256"]) is None
  ):
    raise ToolError("campaign has an invalid toolchain identity")


def _load_toolchain_manifest(path: Path) -> dict[str, str]:
  value = _read_json(path)
  _validate_toolchain(value)
  return value


def _list_benchmarks(binary: Path) -> set[str]:
  result = subprocess.run(
    (str(binary), "--benchmark_list_tests=true"),
    capture_output=True,
    text=True,
    check=False,
    timeout=120,
  )
  if result.returncode != 0:
    raise ToolError(f"cannot list benchmarks in {binary}: {result.stderr}")
  return {line.strip() for line in result.stdout.splitlines() if line.strip()}


def _one_observation(
  binary: Path,
  benchmark_name: str,
  minimum_time_seconds: float,
) -> tuple[dict[str, Any], dict[str, Any]]:
  escaped = re.escape(benchmark_name)
  arguments = (
    f"--benchmark_filter=^{escaped}$",
    f"--benchmark_min_time={minimum_time_seconds}s",
    "--benchmark_repetitions=1",
    "--benchmark_format=json",
  )
  try:
    result = subprocess.run(
      (str(binary), *arguments),
      capture_output=True,
      text=True,
      check=False,
      timeout=1200,
    )
  except (OSError, subprocess.TimeoutExpired) as error:
    raise ToolError(f"cannot run {benchmark_name}: {error}") from error
  if result.returncode != 0:
    raise ToolError(
      f"{benchmark_name} exited {result.returncode}: {result.stderr[-1000:]}"
    )
  try:
    payload = json.loads(result.stdout)
  except json.JSONDecodeError as error:
    raise ToolError(f"{benchmark_name} returned malformed JSON") from error
  runs = [
    entry
    for entry in payload.get("benchmarks", [])
    if entry.get("run_type", "iteration") == "iteration"
  ]
  if len(runs) != 1 or runs[0].get("name") != benchmark_name:
    raise ToolError(f"{benchmark_name} did not return exactly one raw run")
  run = runs[0]
  scale = {"ns": 1.0, "us": 1e3, "ms": 1e6, "s": 1e9}.get(
    run.get("time_unit", "ns")
  )
  if scale is None:
    raise ToolError(f"{benchmark_name} has an unsupported time unit")
  reserved = {
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
  counters = {
    key: value
    for key, value in run.items()
    if key not in reserved and isinstance(value, (int, float))
  }
  peak_rss = counters.get("process_peak_rss_kib")
  observation = {
    "cpu_time_ns": float(run["cpu_time"]) * scale,
    "real_time_ns": float(run["real_time"]) * scale,
    "iterations": int(run["iterations"]),
    "peak_rss_kib": int(peak_rss) if peak_rss is not None else None,
    "counters": counters,
  }
  context = payload.get("context", {})
  safe_context = {
    key: value
    for key, value in context.items()
    if key not in {"date", "executable", "host_name"}
  }
  return observation, safe_context


def collect_campaign(
  binary: Path,
  config: Mapping[str, Any],
  *,
  device: str,
  source_sha: str,
  toolchain: Mapping[str, str],
  repetitions: int,
  minimum_time_seconds: float,
  seed: int,
) -> dict[str, Any]:
  """Collect exact-order randomized observations from one device."""
  _validate_toolchain(toolchain)
  validate_collection_parameters(
    config,
    device=device,
    phase="candidate",
    repetitions=repetitions,
    minimum_time_seconds=minimum_time_seconds,
    seed=seed,
  )
  if not re.fullmatch(r"[0-9a-f]{40}", source_sha):
    raise ToolError("source SHA must be 40 lowercase hexadecimal characters")
  if repetitions < 4 or repetitions % 2 != 0:
    raise ToolError("repetitions must be an even number of at least four")
  if minimum_time_seconds <= 0:
    raise ToolError("minimum time must be positive")
  binary = binary.resolve()
  if not binary.is_file():
    raise ToolError(f"benchmark binary does not exist: {binary}")

  expected_names = {
    name
    for entry in config["workloads"].values()
    for name in entry["benchmarks"].values()
  }
  registered = {
    name
    for name in _list_benchmarks(binary)
    if name.startswith("maintenance/campaign/")
  }
  missing = sorted(expected_names - registered)
  extra = sorted(registered - expected_names)
  if missing or extra:
    raise ToolError(
      f"campaign registry mismatch: missing={missing}, extra={extra}"
    )

  rng = random.Random(seed)
  observations: list[dict[str, Any]] = []
  context: dict[str, Any] = {}
  order = 0
  base_schedule = [
    (workload, backend, name)
    for workload, entry in config["workloads"].items()
    for backend, name in entry["benchmarks"].items()
  ]
  for repetition in range(repetitions):
    schedule = list(base_schedule)
    rng.shuffle(schedule)
    for workload, backend, name in schedule:
      measured, run_context = _one_observation(
        binary, name, minimum_time_seconds
      )
      if not context:
        context = run_context
      observations.append(
        {
          "workload": workload,
          "backend": backend,
          "repetition": repetition,
          "order": order,
          **measured,
        }
      )
      order += 1

  return {
    "schema_version": 1,
    "kind": "maintenance-candidate-campaign",
    "device": device,
    "source_sha": source_sha,
    "binary_sha256": _sha256(binary),
    "tool_sha256": _sha256(Path(__file__).resolve()),
    "config_sha256": mapping_sha256(config),
    "toolchain": dict(toolchain),
    "collection": {
      "seed": seed,
      "repetitions": repetitions,
      "minimum_time_seconds": minimum_time_seconds,
      "metric": "cpu_time_ns",
      "order_policy": "seeded shuffle of every workload/backend per round",
    },
    "environment": _environment(),
    "benchmark_context": context,
    "observations": observations,
  }


def collect_calibration(
  binary: Path,
  config: Mapping[str, Any],
  *,
  device: str,
  source_sha: str,
  toolchain: Mapping[str, str],
  repetitions: int,
  minimum_time_seconds: float,
  seed: int,
) -> dict[str, Any]:
  """Collect seeded A/A blocks before any candidate comparison."""
  _validate_toolchain(toolchain)
  validate_collection_parameters(
    config,
    device=device,
    phase="calibration",
    repetitions=repetitions,
    minimum_time_seconds=minimum_time_seconds,
    seed=seed,
  )
  if not re.fullmatch(r"[0-9a-f]{40}", source_sha):
    raise ToolError("source SHA must be 40 lowercase hexadecimal characters")
  if repetitions < 4 or repetitions % 2 != 0:
    raise ToolError("repetitions must be an even number of at least four")
  binary = binary.resolve()
  if not binary.is_file():
    raise ToolError(f"benchmark binary does not exist: {binary}")
  backend = config["analysis"]["calibration_backend"]
  names = {
    workload: entry["benchmarks"][backend]
    for workload, entry in config["workloads"].items()
  }
  expected_names = {
    name
    for entry in config["workloads"].values()
    for name in entry["benchmarks"].values()
  }
  registered = {
    name
    for name in _list_benchmarks(binary)
    if name.startswith("maintenance/campaign/")
  }
  missing = sorted(expected_names - registered)
  extra = sorted(registered - expected_names)
  if missing or extra:
    raise ToolError(
      f"campaign registry mismatch: missing={missing}, extra={extra}"
    )

  rng = random.Random(seed)
  observations: list[dict[str, Any]] = []
  context: dict[str, Any] = {}
  order = 0
  base_schedule = [
    (workload, side, name)
    for workload, name in names.items()
    for side in ("a", "b")
  ]
  for repetition in range(repetitions):
    schedule = list(base_schedule)
    rng.shuffle(schedule)
    for workload, side, name in schedule:
      measured, run_context = _one_observation(
        binary, name, minimum_time_seconds
      )
      if not context:
        context = run_context
      observations.append(
        {
          "workload": workload,
          "backend": backend,
          "side": side,
          "repetition": repetition,
          "order": order,
          **measured,
        }
      )
      order += 1
  return {
    "schema_version": 1,
    "kind": "maintenance-aa-calibration",
    "device": device,
    "source_sha": source_sha,
    "binary_sha256": _sha256(binary),
    "tool_sha256": _sha256(Path(__file__).resolve()),
    "config_sha256": mapping_sha256(config),
    "toolchain": dict(toolchain),
    "collection": {
      "seed": seed,
      "repetitions": repetitions,
      "minimum_time_seconds": minimum_time_seconds,
      "metric": "cpu_time_ns",
      "order_policy": "seeded shuffle of every workload/A-or-B per block",
    },
    "environment": _environment(),
    "benchmark_context": context,
    "observations": observations,
  }


def _percentile(values: Sequence[float], quantile: float) -> float:
  if not values:
    raise ToolError("cannot calculate a percentile of no values")
  ordered = sorted(values)
  index = min(len(ordered) - 1, math.ceil(quantile * len(ordered)) - 1)
  return ordered[index]


def _validate_observation_order(
  observations: Sequence[Mapping[str, Any]],
) -> None:
  orders = [entry.get("order") for entry in observations]
  if any(
    not isinstance(order, int) or isinstance(order, bool) for order in orders
  ):
    raise ToolError("observations do not contain one exact collection order")
  if sorted(orders) != list(range(len(observations))):
    raise ToolError("observations do not contain one exact collection order")


def _measurement(entry: Mapping[str, Any]) -> float:
  try:
    value = float(entry["cpu_time_ns"])
  except (KeyError, TypeError, ValueError) as error:
    raise ToolError(
      "observation has an invalid CPU-time measurement"
    ) from error
  if not math.isfinite(value) or value <= 0:
    raise ToolError("observation CPU time must be positive and finite")
  return value


def _paired_interval(
  ratios: Sequence[float], *, resamples: int, confidence: float, seed: int
) -> tuple[float, float]:
  if not ratios:
    raise ToolError("cannot bootstrap an empty comparison")
  rng = random.Random(seed)
  estimates = []
  for _ in range(resamples):
    sample = [ratios[rng.randrange(len(ratios))] for _ in ratios]
    estimates.append(statistics.median(sample))
  tail = (1.0 - confidence) / 2.0
  return _percentile(estimates, tail), _percentile(estimates, 1.0 - tail)


def analyze_calibration(
  payload: Mapping[str, Any], config: Mapping[str, Any]
) -> dict[str, Any]:
  """Freeze per-device thresholds from a separate A/A campaign."""
  if payload.get("kind") != "maintenance-aa-calibration":
    raise ToolError("threshold input is not an A/A calibration")
  _validate_toolchain(payload.get("toolchain"))
  repetitions = payload.get("collection", {}).get("repetitions")
  if not isinstance(repetitions, int) or repetitions < 4:
    raise ToolError("calibration has an invalid repetition count")
  observations = payload.get("observations")
  if not isinstance(observations, list):
    raise ToolError("calibration observations must be a list")
  analysis = config["analysis"]
  backend = analysis["calibration_backend"]
  if payload.get("config_sha256") != mapping_sha256(config):
    raise ToolError("calibration config identity does not match policy")
  validate_collection_parameters(
    config,
    device=payload.get("device"),
    phase="calibration",
    repetitions=repetitions,
    minimum_time_seconds=payload.get("collection", {}).get(
      "minimum_time_seconds"
    ),
    seed=payload.get("collection", {}).get("seed"),
  )
  expected_count = repetitions * len(config["workloads"]) * 2
  if len(observations) != expected_count:
    raise ToolError("calibration contains unexpected or missing observations")
  _validate_observation_order(observations)
  workload_thresholds = {}
  for workload in config["workloads"]:
    side_samples = {}
    for side in ("a", "b"):
      selected = sorted(
        (
          entry
          for entry in observations
          if entry.get("workload") == workload
          and entry.get("backend") == backend
          and entry.get("side") == side
        ),
        key=lambda entry: entry.get("repetition", -1),
      )
      if [entry.get("repetition") for entry in selected] != list(
        range(repetitions)
      ):
        raise ToolError(f"{workload}/{side} does not contain all repetitions")
      side_samples[side] = [_measurement(entry) for entry in selected]
    relative_noise = [
      abs(second / first - 1.0)
      for first, second in zip(side_samples["a"], side_samples["b"])
    ]
    absolute_noise = [
      abs(second - first)
      for first, second in zip(side_samples["a"], side_samples["b"])
    ]
    relative_p95 = _percentile(relative_noise, 0.95)
    absolute_p95 = _percentile(absolute_noise, 0.95)
    multiplier = float(analysis["noise_multiplier"])
    workload_thresholds[workload] = {
      "relative_noise_p95": relative_p95,
      "absolute_noise_p95_ns": absolute_p95,
      "relative_threshold": max(
        float(analysis["minimum_relative_effect"]),
        multiplier * relative_p95,
      ),
      "absolute_threshold_ns": max(
        float(analysis["minimum_absolute_effect_ns"]),
        multiplier * absolute_p95,
      ),
      "valid": relative_p95 <= float(analysis["maximum_relative_noise"]),
    }
  return {
    "schema_version": 1,
    "kind": "maintenance-threshold-manifest",
    "device": payload.get("device"),
    "source_sha": payload.get("source_sha"),
    "binary_sha256": payload.get("binary_sha256"),
    "tool_sha256": payload.get("tool_sha256"),
    "config_sha256": payload.get("config_sha256"),
    "toolchain": payload.get("toolchain"),
    "calibration_sha256": hashlib.sha256(
      json.dumps(payload, sort_keys=True).encode("utf-8")
    ).hexdigest(),
    "analysis_policy": analysis,
    "valid": all(entry["valid"] for entry in workload_thresholds.values()),
    "workloads": workload_thresholds,
  }


def analyze_campaign(
  payload: Mapping[str, Any],
  config: Mapping[str, Any],
  thresholds: Mapping[str, Any],
) -> dict[str, Any]:
  """Analyze one device without changing the frozen decision policy."""
  if payload.get("schema_version") != 1:
    raise ToolError("unsupported observation schema")
  if payload.get("kind") != "maintenance-candidate-campaign":
    raise ToolError("analysis input is not a candidate campaign")
  _validate_toolchain(payload.get("toolchain"))
  if thresholds.get("kind") != "maintenance-threshold-manifest":
    raise ToolError("candidate analysis needs a threshold manifest")
  if not thresholds.get("valid"):
    raise ToolError("A/A noise exceeded the predeclared limit")
  if thresholds.get("analysis_policy") != config.get("analysis"):
    raise ToolError("threshold policy differs from the campaign config")
  for identity in (
    "device",
    "source_sha",
    "binary_sha256",
    "tool_sha256",
    "config_sha256",
    "toolchain",
  ):
    if payload.get(identity) != thresholds.get(identity):
      raise ToolError(f"candidate and calibration differ in {identity}")
  if payload.get("config_sha256") != mapping_sha256(config):
    raise ToolError("candidate config identity does not match policy")
  repetitions = payload.get("collection", {}).get("repetitions")
  if not isinstance(repetitions, int) or repetitions < 4:
    raise ToolError("campaign has an invalid repetition count")
  validate_collection_parameters(
    config,
    device=payload.get("device"),
    phase="candidate",
    repetitions=repetitions,
    minimum_time_seconds=payload.get("collection", {}).get(
      "minimum_time_seconds"
    ),
    seed=payload.get("collection", {}).get("seed"),
  )
  observations = payload.get("observations")
  if not isinstance(observations, list):
    raise ToolError("campaign observations must be a list")
  expected_count = (
    repetitions * len(config["workloads"]) * len(config["backends"])
  )
  if len(observations) != expected_count:
    raise ToolError("campaign contains unexpected or missing observations")
  _validate_observation_order(observations)

  analysis = config["analysis"]
  workload_reports = {}
  device_decisions = []
  for workload in config["workloads"]:
    samples: dict[str, list[float]] = {}
    counters: dict[str, list[dict[str, Any]]] = {}
    memory: dict[str, list[int | None]] = {}
    for backend in config["backends"]:
      selected = sorted(
        (
          entry
          for entry in observations
          if entry.get("workload") == workload
          and entry.get("backend") == backend
        ),
        key=lambda entry: entry.get("repetition", -1),
      )
      indices = [entry.get("repetition") for entry in selected]
      if indices != list(range(repetitions)):
        raise ToolError(
          f"{workload}/{backend} does not contain all repetitions"
        )
      samples[backend] = [_measurement(entry) for entry in selected]
      counters[backend] = [entry.get("counters", {}) for entry in selected]
      memory[backend] = [entry.get("peak_rss_kib") for entry in selected]

    threshold = thresholds["workloads"].get(workload)
    if not isinstance(threshold, dict) or not threshold.get("valid"):
      raise ToolError(f"{workload} has no valid frozen threshold")
    relative_threshold = float(threshold["relative_threshold"])
    absolute_threshold = float(threshold["absolute_threshold_ns"])
    medians = {
      backend: statistics.median(values)
      for backend, values in samples.items()
    }
    comparisons = {}
    for comparison_index, control in enumerate(config["controls"]):
      ratios = [
        dirty / baseline - 1.0
        for dirty, baseline in zip(samples["dirty_bit"], samples[control])
      ]
      deltas = [
        dirty - baseline
        for dirty, baseline in zip(samples["dirty_bit"], samples[control])
      ]
      ci_low, ci_high = _paired_interval(
        ratios,
        resamples=int(analysis["bootstrap_resamples"]),
        confidence=float(analysis["confidence"]),
        seed=int(analysis["bootstrap_seed"]) + comparison_index,
      )
      median_delta = statistics.median(deltas)
      decision = "flat"
      if ci_high < -relative_threshold and median_delta < -absolute_threshold:
        decision = "material_win"
      elif ci_low > relative_threshold and median_delta > absolute_threshold:
        decision = "material_regression"
      comparisons[control] = {
        "control_median_ns": medians[control],
        "dirty_median_ns": medians["dirty_bit"],
        "median_relative_delta": statistics.median(ratios),
        "confidence_interval": [ci_low, ci_high],
        "median_absolute_delta_ns": median_delta,
        "decision": decision,
      }
    primary_control = analysis["primary_control"]
    decision = comparisons[primary_control]["decision"]
    device_decisions.extend(
      comparisons[control]["decision"]
      for control in analysis["guardrail_controls"]
    )
    workload_reports[workload] = {
      "primary_control": primary_control,
      "relative_threshold": relative_threshold,
      "absolute_threshold_ns": absolute_threshold,
      "decision": decision,
      "medians_ns": medians,
      "comparisons": comparisons,
      "peak_rss_kib": {
        backend: max(value for value in values if value is not None)
        if any(value is not None for value in values)
        else None
        for backend, values in memory.items()
      },
      "last_counters": {
        backend: values[-1] for backend, values in counters.items()
      },
    }

  primary_control = analysis["primary_control"]
  primary_ratios = []
  primary_deltas = []
  for repetition in range(repetitions):
    dirty_values = []
    control_values = []
    for workload in analysis["primary_workloads"]:
      selected_dirty = next(
        _measurement(entry)
        for entry in observations
        if entry.get("workload") == workload
        and entry.get("backend") == "dirty_bit"
        and entry.get("repetition") == repetition
      )
      selected_control = next(
        _measurement(entry)
        for entry in observations
        if entry.get("workload") == workload
        and entry.get("backend") == primary_control
        and entry.get("repetition") == repetition
      )
      dirty_values.append(selected_dirty)
      control_values.append(selected_control)
    dirty_geomean = math.prod(dirty_values) ** (1.0 / len(dirty_values))
    control_geomean = math.prod(control_values) ** (1.0 / len(control_values))
    primary_ratios.append(dirty_geomean / control_geomean - 1.0)
    primary_deltas.append(dirty_geomean - control_geomean)
  primary_relative_threshold = max(
    float(thresholds["workloads"][workload]["relative_threshold"])
    for workload in analysis["primary_workloads"]
  )
  primary_absolute_threshold = max(
    float(thresholds["workloads"][workload]["absolute_threshold_ns"])
    for workload in analysis["primary_workloads"]
  )
  primary_low, primary_high = _paired_interval(
    primary_ratios,
    resamples=int(analysis["bootstrap_resamples"]),
    confidence=float(analysis["confidence"]),
    seed=int(analysis["bootstrap_seed"]) + 10_000,
  )
  primary_delta = statistics.median(primary_deltas)
  primary_decision = "flat"
  if (
    primary_high < -primary_relative_threshold
    and primary_delta < -primary_absolute_threshold
  ):
    primary_decision = "material_win"
  elif (
    primary_low > primary_relative_threshold
    and primary_delta > primary_absolute_threshold
  ):
    primary_decision = "material_regression"

  if "material_regression" in device_decisions:
    overall = "material_regression"
  elif primary_decision == "material_win":
    overall = "material_win"
  else:
    overall = "flat"
  return {
    "schema_version": 1,
    "kind": "maintenance-device-report",
    "device": payload.get("device"),
    "source_sha": payload.get("source_sha"),
    "binary_sha256": payload.get("binary_sha256"),
    "tool_sha256": payload.get("tool_sha256"),
    "config_sha256": payload.get("config_sha256"),
    "toolchain": payload.get("toolchain"),
    "analysis_policy": analysis,
    "calibration_sha256": thresholds.get("calibration_sha256"),
    "threshold_manifest_sha256": mapping_sha256(thresholds),
    "primary_comparison": {
      "control": primary_control,
      "workloads": analysis["primary_workloads"],
      "median_relative_delta": statistics.median(primary_ratios),
      "confidence_interval": [primary_low, primary_high],
      "median_absolute_delta_ns": primary_delta,
      "relative_threshold": primary_relative_threshold,
      "absolute_threshold_ns": primary_absolute_threshold,
      "decision": primary_decision,
    },
    "overall_decision": overall,
    "workloads": workload_reports,
  }


def cross_device_decision(
  reports: Sequence[Mapping[str, Any]], config: Mapping[str, Any]
) -> dict[str, Any]:
  """Apply the portable promotion rule to exactly two device reports."""
  if any(
    report.get("schema_version") != 1
    or report.get("kind") != "maintenance-device-report"
    for report in reports
  ):
    raise ToolError("cross-device input is not a device report")
  devices = {report.get("device") for report in reports}
  if len(reports) != 2 or len(devices) != 2:
    raise ToolError("cross-device decision needs two distinct devices")
  expected_devices = set(config["collection"]["devices"])
  if devices != expected_devices:
    raise ToolError("device reports do not match the frozen device set")
  source_shas = {report.get("source_sha") for report in reports}
  if len(source_shas) != 1:
    raise ToolError("device reports measured different source commits")
  tool_shas = {report.get("tool_sha256") for report in reports}
  if len(tool_shas) != 1 or None in tool_shas:
    raise ToolError("device reports used different campaign tools")
  config_sha = mapping_sha256(config)
  if any(report.get("config_sha256") != config_sha for report in reports):
    raise ToolError("device report config identity does not match policy")
  decisions = [report.get("overall_decision") for report in reports]
  allowed = {"material_win", "material_regression", "flat"}
  if any(decision not in allowed for decision in decisions):
    raise ToolError("device report contains an invalid decision")
  promote = (
    "material_win" in decisions and "material_regression" not in decisions
  )
  return {
    "schema_version": 1,
    "source_sha": next(iter(source_shas)),
    "devices": [report.get("device") for report in reports],
    "device_decisions": decisions,
    "decision": "promote" if promote else "keep_experimental",
  }


def _read_json(path: Path) -> dict[str, Any]:
  try:
    value = json.loads(path.read_text(encoding="utf-8"))
  except (OSError, json.JSONDecodeError) as error:
    raise ToolError(f"cannot load {path}: {error}") from error
  if not isinstance(value, dict):
    raise ToolError(f"{path}: expected a JSON object")
  return value


def _write_json(path: Path, payload: Mapping[str, Any]) -> None:
  path.parent.mkdir(parents=True, exist_ok=True)
  path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n",
                  encoding="utf-8")


def _parser() -> argparse.ArgumentParser:
  parser = argparse.ArgumentParser(description=__doc__)
  subparsers = parser.add_subparsers(dest="command", required=True)
  collect = subparsers.add_parser("collect")
  collect.add_argument("--binary", type=Path, required=True)
  collect.add_argument("--config", type=Path, required=True)
  collect.add_argument("--device", required=True)
  collect.add_argument("--source-sha", required=True)
  collect.add_argument("--toolchain-manifest", type=Path, required=True)
  collect.add_argument("--repetitions", type=int, default=30)
  collect.add_argument("--minimum-time", type=float, default=0.05)
  collect.add_argument("--seed", type=int, required=True)
  collect.add_argument("--output", type=Path, required=True)
  calibrate = subparsers.add_parser("calibrate")
  calibrate.add_argument("--binary", type=Path, required=True)
  calibrate.add_argument("--config", type=Path, required=True)
  calibrate.add_argument("--device", required=True)
  calibrate.add_argument("--source-sha", required=True)
  calibrate.add_argument("--toolchain-manifest", type=Path, required=True)
  calibrate.add_argument("--repetitions", type=int, default=30)
  calibrate.add_argument("--minimum-time", type=float, default=0.05)
  calibrate.add_argument("--seed", type=int, required=True)
  calibrate.add_argument("--output", type=Path, required=True)
  thresholds = subparsers.add_parser("thresholds")
  thresholds.add_argument("--input", type=Path, required=True)
  thresholds.add_argument("--config", type=Path, required=True)
  thresholds.add_argument("--output", type=Path, required=True)
  analyze = subparsers.add_parser("analyze")
  analyze.add_argument("--input", type=Path, required=True)
  analyze.add_argument("--config", type=Path, required=True)
  analyze.add_argument("--thresholds", type=Path, required=True)
  analyze.add_argument("--output", type=Path, required=True)
  decide = subparsers.add_parser("decide")
  decide.add_argument("--report", type=Path, action="append", required=True)
  decide.add_argument("--config", type=Path, required=True)
  decide.add_argument("--output", type=Path, required=True)
  toolchain = subparsers.add_parser("toolchain")
  toolchain.add_argument("--compiler", type=Path, required=True)
  toolchain.add_argument("--output", type=Path, required=True)
  return parser


def main(argv: Sequence[str] | None = None) -> int:
  args = _parser().parse_args(argv)
  try:
    if args.command == "toolchain":
      payload = _toolchain(args.compiler)
    elif args.command == "collect":
      payload = collect_campaign(
        args.binary,
        load_config(args.config),
        device=args.device,
        source_sha=args.source_sha,
        toolchain=_load_toolchain_manifest(args.toolchain_manifest),
        repetitions=args.repetitions,
        minimum_time_seconds=args.minimum_time,
        seed=args.seed,
      )
    elif args.command == "calibrate":
      payload = collect_calibration(
        args.binary,
        load_config(args.config),
        device=args.device,
        source_sha=args.source_sha,
        toolchain=_load_toolchain_manifest(args.toolchain_manifest),
        repetitions=args.repetitions,
        minimum_time_seconds=args.minimum_time,
        seed=args.seed,
      )
    elif args.command == "thresholds":
      payload = analyze_calibration(
        _read_json(args.input), load_config(args.config)
      )
    elif args.command == "analyze":
      payload = analyze_campaign(
        _read_json(args.input),
        load_config(args.config),
        _read_json(args.thresholds),
      )
    else:
      payload = cross_device_decision(
        [_read_json(path) for path in args.report], load_config(args.config)
      )
    _write_json(args.output, payload)
  except ToolError as error:
    print(f"maintenance campaign: {error}", file=sys.stderr)
    return 2
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
