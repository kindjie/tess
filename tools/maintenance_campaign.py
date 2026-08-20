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
import shlex
import statistics
import subprocess
import sys
from collections.abc import Mapping, Sequence
from pathlib import Path
from typing import Any


class ToolError(RuntimeError):
  """A malformed input or failed campaign command."""


_EMBEDDED_IDENTITY_FIELDS = {
  "tess_source_sha": "source_sha",
  "tess_config_file_sha256": "config_file_sha256",
  "tess_tool_sha256": "tool_sha256",
  "tess_benchmark_source_sha256": "benchmark_source_sha256",
}


def _validate_embedded_identity(
  context: Mapping[str, Any], identity: Mapping[str, Any]
) -> None:
  expected = {
    context_name: identity.get(identity_name)
    for context_name, identity_name in _EMBEDDED_IDENTITY_FIELDS.items()
  }
  if any(
    not isinstance(value, str) or context.get(name) != value
    for name, value in expected.items()
  ):
    raise ToolError("benchmark context has the wrong embedded identity")


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
    counter_expectations = (
      entry.get("expected") if isinstance(entry, dict) else None
    )
    if not isinstance(benchmarks, dict) or set(benchmarks) != expected:
      raise ToolError(
        f"{path}: workload {workload!r} must map every backend exactly"
      )
    if not isinstance(counter_expectations, dict) or set(
      counter_expectations
    ) != {"registered_tasks", "active_tasks", "offers"}:
      raise ToolError(
        f"{path}: workload {workload!r} must declare exact counters"
      )
    if any(
      not isinstance(value, int)
      or isinstance(value, bool)
      or value < 1
      for value in counter_expectations.values()
    ):
      raise ToolError(
        f"{path}: workload {workload!r} counters must be positive integers"
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
    if set(seeds) != {
      "calibration_seed",
      "candidate_seed",
      "build_context",
      "environment",
    }:
      raise ToolError(
        f"{path}: {device!r} must declare seeds and environment"
      )
    if any(
      not isinstance(seed, int) or isinstance(seed, bool)
      for seed in (seeds["calibration_seed"], seeds["candidate_seed"])
    ):
      raise ToolError(f"{path}: {device!r} seeds must be integers")
    if (
      not isinstance(seeds["build_context"], str)
      or not seeds["build_context"]
    ):
      raise ToolError(f"{path}: {device!r} must declare a build context")
    environment = seeds["environment"]
    if not isinstance(environment, dict) or set(environment) != {
      "system",
      "machine",
      "cpu_model_pattern",
    }:
      raise ToolError(f"{path}: {device!r} has invalid environment policy")
    if any(
      not isinstance(value, str) or not value
      for value in environment.values()
    ):
      raise ToolError(f"{path}: {device!r} environment values must be strings")
    try:
      re.compile(environment["cpu_model_pattern"])
    except re.error as error:
      raise ToolError(
        f"{path}: {device!r} has invalid CPU model pattern"
      ) from error
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


def validate_device_environment(
  config: Mapping[str, Any], device: str, environment: Mapping[str, Any]
) -> None:
  policy = config["collection"]["devices"][device]["environment"]
  if environment.get("system") != policy["system"]:
    raise ToolError(f"device {device!r} has the wrong operating system")
  if environment.get("machine") != policy["machine"]:
    raise ToolError(f"device {device!r} has the wrong machine architecture")
  cpu_model = environment.get("cpu_model")
  if (
    not isinstance(cpu_model, str)
    or re.fullmatch(policy["cpu_model_pattern"], cpu_model) is None
  ):
    raise ToolError(f"device {device!r} has the wrong CPU model")


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
    "python_version": platform.python_version(),
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


def _run_git(source_root: Path, *arguments: str) -> str:
  result = subprocess.run(
    ("git", "-C", str(source_root), *arguments),
    capture_output=True,
    text=True,
    check=False,
    timeout=30,
  )
  if result.returncode != 0:
    raise ToolError(f"cannot inspect source tree: {result.stderr.strip()}")
  return result.stdout.strip()


def _sanitize_build_command(
  command: str,
  *,
  source_root: Path,
  build_root: Path,
  compiler: Path,
) -> str:
  replacements = (
    (str(source_root.resolve()), "$SOURCE"),
    (str(build_root.resolve()), "$BUILD"),
    (str(compiler.resolve()), "$COMPILER"),
  )
  sanitized = command
  for original, replacement in sorted(
    replacements, key=lambda entry: len(entry[0]), reverse=True
  ):
    sanitized = sanitized.replace(original, replacement)
  if (
    str(source_root.resolve()) in sanitized
    or str(build_root.resolve()) in sanitized
  ):
    raise ToolError("build command contains an unsanitized local path")
  return sanitized


def create_build_manifest(
  *,
  source_root: Path,
  source_sha: str,
  binary: Path,
  config_path: Path,
  compiler: Path,
  compile_commands: Path,
  link_command: Path,
  device: str,
  build_context: str,
) -> dict[str, Any]:
  """Bind an exact clean source tree to its benchmark build inputs."""
  source_root = source_root.resolve()
  binary = binary.resolve()
  compiler = compiler.resolve()
  compile_commands = compile_commands.resolve()
  link_command = link_command.resolve()
  config = load_config(config_path)
  if device not in config["collection"]["devices"]:
    raise ToolError(f"device {device!r} is not in the frozen campaign")
  if build_context != config["collection"]["devices"][device]["build_context"]:
    raise ToolError("build context differs from the frozen campaign")
  if not re.fullmatch(r"[0-9a-f]{40}", source_sha):
    raise ToolError("source SHA must be 40 lowercase hexadecimal characters")
  if _run_git(source_root, "rev-parse", "HEAD") != source_sha:
    raise ToolError("source SHA does not identify the checked-out commit")
  if _run_git(source_root, "status", "--porcelain", "--untracked-files=no"):
    raise ToolError("build manifest requires a clean tracked source tree")
  if (
    not binary.is_file()
    or not compile_commands.is_file()
    or not link_command.is_file()
  ):
    raise ToolError("build manifest input does not exist")
  benchmark_source = (
    source_root / "bench" / "tess_maintenance_campaign_bench.cc"
  )
  try:
    entries = json.loads(compile_commands.read_text(encoding="utf-8"))
  except (OSError, json.JSONDecodeError) as error:
    raise ToolError(f"cannot load compile commands: {error}") from error
  if not isinstance(entries, list):
    raise ToolError("compile commands must be a JSON array")
  build_root = compile_commands.parent
  def command_source(entry: Mapping[str, Any]) -> Path:
    source = Path(str(entry.get("file", "")))
    directory = Path(str(entry.get("directory", build_root)))
    if source.is_absolute():
      return source.resolve()
    return (directory / source).resolve()

  matching = [
    entry
    for entry in entries
    if isinstance(entry, dict)
    and command_source(entry) == benchmark_source.resolve()
  ]
  if len(matching) != 1:
    raise ToolError("compile commands must contain exactly one campaign source")
  entry = matching[0]
  if isinstance(entry.get("arguments"), list):
    compile_command = shlex.join(str(value) for value in entry["arguments"])
  elif isinstance(entry.get("command"), str):
    compile_command = entry["command"]
  else:
    raise ToolError("campaign compile command is malformed")
  try:
    link_text = link_command.read_text(encoding="utf-8").strip()
  except OSError as error:
    raise ToolError(f"cannot load link command: {error}") from error
  if not link_text:
    raise ToolError("campaign link command is empty")
  compile_text = _sanitize_build_command(
    compile_command,
    source_root=source_root,
    build_root=build_root,
    compiler=compiler,
  )
  link_text = _sanitize_build_command(
    link_text,
    source_root=source_root,
    build_root=build_root,
    compiler=compiler,
  )
  identity = {
    "source_sha": source_sha,
    "config_file_sha256": _sha256(config_path),
    "tool_sha256": _sha256(Path(__file__).resolve()),
    "benchmark_source_sha256": _sha256(benchmark_source),
  }
  probe_name = config["workloads"][sorted(config["workloads"])[0]][
    "benchmarks"
  ]["dirty_bit"]
  probe = _one_observation(binary, probe_name, 0.001)
  _validate_embedded_identity(probe["benchmark_context"], identity)
  return {
    "schema_version": 1,
    "kind": "maintenance-build-manifest",
    "device": device,
    "source_sha": identity["source_sha"],
    "binary_sha256": _sha256(binary),
    "config_sha256": mapping_sha256(config),
    "config_file_sha256": identity["config_file_sha256"],
    "tool_sha256": identity["tool_sha256"],
    "benchmark_source_sha256": identity["benchmark_source_sha256"],
    "embedded_identity": identity,
    "toolchain": _toolchain(compiler),
    "build_context": build_context,
    "compile_command": {
      "text": compile_text,
      "sha256": hashlib.sha256(compile_text.encode("utf-8")).hexdigest(),
    },
    "link_command": {
      "text": link_text,
      "sha256": hashlib.sha256(link_text.encode("utf-8")).hexdigest(),
    },
    "source_tree_clean": True,
  }


def validate_build_manifest(
  manifest: Mapping[str, Any],
  *,
  binary: Path,
  config: Mapping[str, Any],
  config_file_sha256: str,
  device: str,
) -> None:
  required = {
    "schema_version",
    "kind",
    "device",
    "source_sha",
    "binary_sha256",
    "config_sha256",
    "config_file_sha256",
    "tool_sha256",
    "benchmark_source_sha256",
    "embedded_identity",
    "toolchain",
    "build_context",
    "compile_command",
    "link_command",
    "source_tree_clean",
  }
  if set(manifest) != required:
    raise ToolError("build manifest has an incomplete schema")
  if (
    manifest.get("schema_version") != 1
    or manifest.get("kind") != "maintenance-build-manifest"
    or manifest.get("device") != device
    or manifest.get("source_tree_clean") is not True
  ):
    raise ToolError("build manifest does not identify this campaign")
  digest_fields = (
    "binary_sha256",
    "config_sha256",
    "config_file_sha256",
    "tool_sha256",
    "benchmark_source_sha256",
  )
  if any(
    not isinstance(manifest.get(name), str)
    or re.fullmatch(r"[0-9a-f]{64}", manifest[name]) is None
    for name in digest_fields
  ):
    raise ToolError("build manifest contains an invalid digest")
  if (
    not isinstance(manifest.get("source_sha"), str)
    or re.fullmatch(r"[0-9a-f]{40}", manifest["source_sha"]) is None
  ):
    raise ToolError("build manifest contains an invalid source SHA")
  binary = binary.resolve()
  if not binary.is_file() or _sha256(binary) != manifest["binary_sha256"]:
    raise ToolError("benchmark binary differs from the build manifest")
  if mapping_sha256(config) != manifest["config_sha256"]:
    raise ToolError("config differs from the build manifest")
  if config_file_sha256 != manifest["config_file_sha256"]:
    raise ToolError("config file differs from the build manifest")
  root = Path(__file__).resolve().parents[1]
  benchmark_source = root / "bench" / "tess_maintenance_campaign_bench.cc"
  if _sha256(Path(__file__).resolve()) != manifest["tool_sha256"]:
    raise ToolError("collector differs from the build manifest")
  if _sha256(benchmark_source) != manifest["benchmark_source_sha256"]:
    raise ToolError("benchmark source differs from the build manifest")
  identity = {
    name: manifest[name]
    for name in (
      "source_sha",
      "config_file_sha256",
      "tool_sha256",
      "benchmark_source_sha256",
    )
  }
  if manifest.get("embedded_identity") != identity:
    raise ToolError("build manifest has an invalid embedded identity")
  policy = config["collection"]["devices"][device]
  if manifest.get("build_context") != policy["build_context"]:
    raise ToolError("build context differs from the frozen campaign")
  _validate_toolchain(manifest.get("toolchain"))
  for name in ("compile_command", "link_command"):
    command = manifest.get(name)
    if not isinstance(command, dict) or set(command) != {"text", "sha256"}:
      raise ToolError(f"build manifest has an invalid {name}")
    text = command["text"]
    digest = command["sha256"]
    if (
      not isinstance(text, str)
      or not text
      or not isinstance(digest, str)
      or hashlib.sha256(text.encode("utf-8")).hexdigest() != digest
    ):
      raise ToolError(f"build manifest has an invalid {name}")


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
) -> dict[str, Any]:
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
    "stderr": result.stderr.strip(),
  }
  context = payload.get("context", {})
  safe_context = {
    key: value
    for key, value in context.items()
    if key not in {"date", "executable", "host_name"}
  }
  observation["benchmark_context"] = safe_context
  return observation


def _schedule_rank(seed: int, repetition: int, *parts: str) -> str:
  value = ":".join((str(seed), str(repetition), *parts))
  return hashlib.sha256(value.encode("utf-8")).hexdigest()


def _candidate_schedule(
  config: Mapping[str, Any], seed: int, repetition: int
) -> list[tuple[str, str, str]]:
  cells = [
    (workload, backend, entry["benchmarks"][backend])
    for workload, entry in sorted(config["workloads"].items())
    for backend in sorted(config["backends"])
  ]
  return sorted(
    cells,
    key=lambda cell: _schedule_rank(seed, repetition, cell[0], cell[1]),
  )


def _calibration_schedule(
  config: Mapping[str, Any], seed: int, repetition: int
) -> list[tuple[str, str, str]]:
  backend = config["analysis"]["calibration_backend"]
  cells = [
    (workload, side, entry["benchmarks"][backend])
    for workload, entry in sorted(config["workloads"].items())
    for side in ("a", "b")
  ]
  return sorted(
    cells,
    key=lambda cell: _schedule_rank(seed, repetition, cell[0], cell[1]),
  )


def _validate_threshold_for_collection(
  thresholds: Mapping[str, Any],
  *,
  config: Mapping[str, Any],
  build_manifest: Mapping[str, Any],
  build_manifest_sha256: str,
  environment: Mapping[str, Any],
) -> None:
  _validate_threshold_manifest(thresholds, config)
  if thresholds.get("valid") is not True:
    raise ToolError("candidate collection needs valid frozen thresholds")
  expected = {
    "device": build_manifest["device"],
    "source_sha": build_manifest["source_sha"],
    "binary_sha256": build_manifest["binary_sha256"],
    "config_file_sha256": build_manifest["config_file_sha256"],
    "tool_sha256": build_manifest["tool_sha256"],
    "benchmark_source_sha256": build_manifest[
      "benchmark_source_sha256"
    ],
    "config_sha256": build_manifest["config_sha256"],
    "toolchain": build_manifest["toolchain"],
    "build_manifest_sha256": build_manifest_sha256,
    "environment": environment,
  }
  if any(thresholds.get(name) != value for name, value in expected.items()):
    raise ToolError("threshold identity differs from the candidate build")
  if thresholds.get("analysis_policy") != config["analysis"]:
    raise ToolError("threshold policy differs from the campaign config")


def _validate_threshold_manifest(
  thresholds: Mapping[str, Any], config: Mapping[str, Any]
) -> None:
  required = {
    "schema_version",
    "kind",
    "device",
    "source_sha",
    "binary_sha256",
    "config_sha256",
    "config_file_sha256",
    "tool_sha256",
    "benchmark_source_sha256",
    "build_manifest_sha256",
    "toolchain",
    "environment",
    "calibration_sha256",
    "analysis_policy",
    "valid",
    "workloads",
  }
  if (
    set(thresholds) != required
    or thresholds.get("schema_version") != 1
    or thresholds.get("kind") != "maintenance-threshold-manifest"
  ):
    raise ToolError("threshold manifest has an incomplete schema")
  _validate_payload_identity(thresholds)
  calibration_sha = thresholds.get("calibration_sha256")
  if not isinstance(calibration_sha, str) or re.fullmatch(
    r"[0-9a-f]{64}", calibration_sha
  ) is None:
    raise ToolError("threshold manifest has an invalid calibration identity")
  if thresholds.get("analysis_policy") != config["analysis"]:
    raise ToolError("threshold policy differs from the campaign config")
  if not isinstance(thresholds.get("valid"), bool):
    raise ToolError("threshold manifest has an invalid verdict")
  workloads = thresholds.get("workloads")
  if not isinstance(workloads, dict) or set(workloads) != set(
    config["workloads"]
  ):
    raise ToolError("threshold manifest has an incomplete workload set")
  required_workload = {
    "relative_noise_p95",
    "absolute_noise_p95_ns",
    "relative_threshold",
    "absolute_threshold_ns",
    "valid",
  }
  for workload in workloads.values():
    if not isinstance(workload, dict) or set(workload) != required_workload:
      raise ToolError("threshold manifest has an incomplete workload result")
    if (
      not all(
        _finite_number(workload[name]) and workload[name] >= 0
        for name in required_workload - {"valid"}
      )
      or not isinstance(workload["valid"], bool)
    ):
      raise ToolError("threshold manifest has an invalid workload result")
  if thresholds["valid"] != all(
    workload["valid"] for workload in workloads.values()
  ):
    raise ToolError("threshold manifest has an inconsistent verdict")


def collect_campaign(
  binary: Path,
  config: Mapping[str, Any],
  *,
  device: str,
  config_file_sha256: str,
  build_manifest: Mapping[str, Any],
  thresholds: Mapping[str, Any],
  repetitions: int,
  minimum_time_seconds: float,
  seed: int,
) -> dict[str, Any]:
  """Collect exact-order randomized observations from one device."""
  validate_collection_parameters(
    config,
    device=device,
    phase="candidate",
    repetitions=repetitions,
    minimum_time_seconds=minimum_time_seconds,
    seed=seed,
  )
  if repetitions < 4 or repetitions % 2 != 0:
    raise ToolError("repetitions must be an even number of at least four")
  if minimum_time_seconds <= 0:
    raise ToolError("minimum time must be positive")
  binary = binary.resolve()
  if not binary.is_file():
    raise ToolError(f"benchmark binary does not exist: {binary}")
  validate_build_manifest(
    build_manifest,
    binary=binary,
    config=config,
    config_file_sha256=config_file_sha256,
    device=device,
  )
  environment = _environment()
  validate_device_environment(config, device, environment)
  build_manifest_sha256 = mapping_sha256(build_manifest)
  _validate_threshold_for_collection(
    thresholds,
    config=config,
    build_manifest=build_manifest,
    build_manifest_sha256=build_manifest_sha256,
    environment=environment,
  )

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

  observations: list[dict[str, Any]] = []
  order = 0
  for repetition in range(repetitions):
    for workload, backend, name in _candidate_schedule(
      config, seed, repetition
    ):
      measured = _one_observation(binary, name, minimum_time_seconds)
      _validate_embedded_identity(
        measured["benchmark_context"], build_manifest
      )
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
    "source_sha": build_manifest["source_sha"],
    "binary_sha256": build_manifest["binary_sha256"],
    "tool_sha256": build_manifest["tool_sha256"],
    "config_file_sha256": build_manifest["config_file_sha256"],
    "benchmark_source_sha256": build_manifest[
      "benchmark_source_sha256"
    ],
    "config_sha256": build_manifest["config_sha256"],
    "toolchain": build_manifest["toolchain"],
    "build_manifest_sha256": build_manifest_sha256,
    "threshold_manifest_sha256": mapping_sha256(thresholds),
    "calibration_sha256": thresholds["calibration_sha256"],
    "collection": {
      "seed": seed,
      "repetitions": repetitions,
      "minimum_time_seconds": minimum_time_seconds,
      "metric": "cpu_time_ns",
      "order_policy": "SHA-256 rank of seed/block/workload/backend",
    },
    "environment": environment,
    "observations": observations,
  }


def collect_calibration(
  binary: Path,
  config: Mapping[str, Any],
  *,
  device: str,
  config_file_sha256: str,
  build_manifest: Mapping[str, Any],
  repetitions: int,
  minimum_time_seconds: float,
  seed: int,
) -> dict[str, Any]:
  """Collect seeded A/A blocks before any candidate comparison."""
  validate_collection_parameters(
    config,
    device=device,
    phase="calibration",
    repetitions=repetitions,
    minimum_time_seconds=minimum_time_seconds,
    seed=seed,
  )
  if repetitions < 4 or repetitions % 2 != 0:
    raise ToolError("repetitions must be an even number of at least four")
  binary = binary.resolve()
  if not binary.is_file():
    raise ToolError(f"benchmark binary does not exist: {binary}")
  validate_build_manifest(
    build_manifest,
    binary=binary,
    config=config,
    config_file_sha256=config_file_sha256,
    device=device,
  )
  environment = _environment()
  validate_device_environment(config, device, environment)
  build_manifest_sha256 = mapping_sha256(build_manifest)
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

  observations: list[dict[str, Any]] = []
  order = 0
  for repetition in range(repetitions):
    for workload, side, name in _calibration_schedule(
      config, seed, repetition
    ):
      measured = _one_observation(binary, name, minimum_time_seconds)
      _validate_embedded_identity(
        measured["benchmark_context"], build_manifest
      )
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
    "source_sha": build_manifest["source_sha"],
    "binary_sha256": build_manifest["binary_sha256"],
    "tool_sha256": build_manifest["tool_sha256"],
    "config_file_sha256": build_manifest["config_file_sha256"],
    "benchmark_source_sha256": build_manifest[
      "benchmark_source_sha256"
    ],
    "config_sha256": build_manifest["config_sha256"],
    "toolchain": build_manifest["toolchain"],
    "build_manifest_sha256": build_manifest_sha256,
    "collection": {
      "seed": seed,
      "repetitions": repetitions,
      "minimum_time_seconds": minimum_time_seconds,
      "metric": "cpu_time_ns",
      "order_policy": "SHA-256 rank of seed/block/workload/A-or-B",
    },
    "environment": environment,
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
  config: Mapping[str, Any],
  *,
  phase: str,
  seed: int,
  repetitions: int,
) -> None:
  orders = [entry.get("order") for entry in observations]
  if any(
    not isinstance(order, int) or isinstance(order, bool) for order in orders
  ):
    raise ToolError("observations do not contain one exact collection order")
  if sorted(orders) != list(range(len(observations))):
    raise ToolError("observations do not contain one exact collection order")
  actual = sorted(observations, key=lambda entry: entry["order"])
  expected: list[tuple[Any, ...]] = []
  if phase == "candidate":
    for repetition in range(repetitions):
      expected.extend(
        (workload, backend, repetition)
        for workload, backend, _ in _candidate_schedule(
          config, seed, repetition
        )
      )
    observed = [
      (entry.get("workload"), entry.get("backend"), entry.get("repetition"))
      for entry in actual
    ]
  else:
    backend = config["analysis"]["calibration_backend"]
    for repetition in range(repetitions):
      expected.extend(
        (workload, backend, side, repetition)
        for workload, side, _ in _calibration_schedule(
          config, seed, repetition
        )
      )
    observed = [
      (
        entry.get("workload"),
        entry.get("backend"),
        entry.get("side"),
        entry.get("repetition"),
      )
      for entry in actual
    ]
  if observed != expected:
    raise ToolError("observations differ from the frozen collection schedule")


def _measurement(entry: Mapping[str, Any]) -> float:
  try:
    value = float(entry["cpu_time_ns"])
    real_time = float(entry["real_time_ns"])
  except (KeyError, TypeError, ValueError) as error:
    raise ToolError(
      "observation has an invalid CPU-time measurement"
    ) from error
  if (
    not math.isfinite(value)
    or value <= 0
    or not math.isfinite(real_time)
    or real_time <= 0
  ):
    raise ToolError("observation times must be positive and finite")
  iterations = entry.get("iterations")
  peak_rss = entry.get("peak_rss_kib")
  if (
    not isinstance(iterations, int)
    or isinstance(iterations, bool)
    or iterations < 1
    or not isinstance(peak_rss, int)
    or isinstance(peak_rss, bool)
    or peak_rss < 1
  ):
    raise ToolError("observation iterations and RSS must be positive integers")
  if not isinstance(entry.get("stderr"), str):
    raise ToolError("observation must retain benchmark stderr")
  context = entry.get("benchmark_context")
  required_context = {
    "num_cpus",
    "cpu_scaling_enabled",
    "library_version",
    "library_build_type",
    "json_schema_version",
  }
  if not isinstance(context, dict) or not required_context <= set(context):
    raise ToolError("observation is missing benchmark context")
  if context["cpu_scaling_enabled"] is not False:
    raise ToolError("benchmark ran with CPU frequency scaling enabled")
  return value


def _validate_payload_identity(payload: Mapping[str, Any]) -> None:
  source_sha = payload.get("source_sha")
  if not isinstance(source_sha, str) or re.fullmatch(
    r"[0-9a-f]{40}", source_sha
  ) is None:
    raise ToolError("campaign payload has an invalid source SHA")
  for name in (
    "binary_sha256",
    "config_sha256",
    "config_file_sha256",
    "tool_sha256",
    "benchmark_source_sha256",
    "build_manifest_sha256",
  ):
    value = payload.get(name)
    if (
      not isinstance(value, str)
      or re.fullmatch(r"[0-9a-f]{64}", value) is None
    ):
      raise ToolError(f"campaign payload has an invalid {name}")
  _validate_toolchain(payload.get("toolchain"))


def _validate_observation_contexts(
  observations: Sequence[Mapping[str, Any]],
  payload: Mapping[str, Any],
) -> None:
  invariant_fields = {
    "num_cpus",
    "cpu_scaling_enabled",
    "library_version",
    "library_build_type",
    "json_schema_version",
    *_EMBEDDED_IDENTITY_FIELDS,
  }
  expected: dict[str, Any] | None = None
  for entry in observations:
    _measurement(entry)
    context = entry["benchmark_context"]
    _validate_embedded_identity(context, payload)
    invariant = {name: context.get(name) for name in invariant_fields}
    if expected is None:
      expected = invariant
    elif invariant != expected:
      raise ToolError("benchmark context drifted during collection")
  environment = payload.get("environment")
  if (
    expected is None
    or not isinstance(environment, dict)
    or expected["num_cpus"] != environment.get("logical_cpus")
  ):
    raise ToolError("benchmark context drifted from the device environment")


def _validate_work_counters(
  entry: Mapping[str, Any], expected: Mapping[str, int]
) -> None:
  counters = entry.get("counters")
  required = {
    "offers",
    "rebuilds",
    "drain_calls",
    "schedule_calls",
    "coalesced_calls",
    "executions",
    "capacity_failures",
    "scanned_values",
    "active_tasks",
    "authoritative_checksum",
    "resident_slots",
  }
  if not isinstance(counters, dict) or not required <= set(counters):
    raise ToolError("observation is missing required work counters")
  try:
    values = {name: float(counters[name]) for name in required}
  except (TypeError, ValueError) as error:
    raise ToolError("observation has an invalid work counter") from error
  if any(not math.isfinite(value) or value < 0 for value in values.values()):
    raise ToolError("work counters must be nonnegative and finite")
  exact = {
    "offers": expected["offers"],
    "active_tasks": expected["active_tasks"],
    "resident_slots": expected["registered_tasks"],
    "capacity_failures": 0,
  }
  if any(values[name] != value for name, value in exact.items()):
    raise ToolError("work counters differ from the frozen workload")
  if values["offers"] != values["schedule_calls"]:
    raise ToolError("offer and schedule counters differ")
  if (
    values["executions"] + values["coalesced_calls"]
    != values["schedule_calls"]
  ):
    raise ToolError("terminal schedule counters do not conserve offers")
  if values["rebuilds"] > values["executions"] or values["drain_calls"] < 1:
    raise ToolError("execution work counters are inconsistent")


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


def _overall_decision(
  primary_decision: str,
  workload_reports: Mapping[str, Any],
  analysis: Mapping[str, Any],
) -> str:
  guardrails = [
    report["comparisons"][control]["decision"]
    for report in workload_reports.values()
    for control in analysis["guardrail_controls"]
  ]
  if (
    primary_decision == "material_regression"
    or "material_regression" in guardrails
  ):
    return "material_regression"
  if primary_decision == "inconclusive" or "inconclusive" in guardrails:
    return "inconclusive"
  if primary_decision == "material_win":
    return "material_win"
  return "flat"


def _finite_number(value: Any) -> bool:
  return (
    isinstance(value, (int, float))
    and not isinstance(value, bool)
    and math.isfinite(float(value))
  )


def _validate_interval(value: Any, *, name: str) -> tuple[float, float]:
  if (
    not isinstance(value, list)
    or len(value) != 2
    or not all(_finite_number(bound) for bound in value)
    or float(value[0]) > float(value[1])
  ):
    raise ToolError(f"device report has an invalid {name}")
  return float(value[0]), float(value[1])


def _classify_interval(
  relative_interval: tuple[float, float],
  absolute_interval: tuple[float, float],
  relative_threshold: float,
  absolute_threshold: float,
) -> str:
  relative_low, relative_high = relative_interval
  absolute_low, absolute_high = absolute_interval
  if (
    relative_high < -relative_threshold
    and absolute_high < -absolute_threshold
  ):
    return "material_win"
  if (
    relative_low > relative_threshold
    and absolute_low > absolute_threshold
  ):
    return "material_regression"
  if (
    relative_high > relative_threshold
    and absolute_high > absolute_threshold
  ):
    return "inconclusive"
  return "flat"


def _validate_comparison_record(
  comparison: Any,
  *,
  relative_threshold: float,
  absolute_threshold: float,
) -> None:
  expected = {
    "control_median_ns",
    "dirty_median_ns",
    "median_relative_delta",
    "confidence_interval",
    "median_absolute_delta_ns",
    "absolute_confidence_interval_ns",
    "decision",
  }
  if not isinstance(comparison, dict) or set(comparison) != expected:
    raise ToolError("device report has an incomplete comparison")
  numeric = (
    "control_median_ns",
    "dirty_median_ns",
    "median_relative_delta",
    "median_absolute_delta_ns",
  )
  if any(not _finite_number(comparison[name]) for name in numeric):
    raise ToolError("device report has an invalid comparison measurement")
  if (
    float(comparison["control_median_ns"]) <= 0
    or float(comparison["dirty_median_ns"]) <= 0
  ):
    raise ToolError("device report has a nonpositive comparison median")
  relative_interval = _validate_interval(
    comparison["confidence_interval"], name="comparison interval"
  )
  absolute_interval = _validate_interval(
    comparison["absolute_confidence_interval_ns"],
    name="absolute comparison interval",
  )
  expected_decision = _classify_interval(
    relative_interval,
    absolute_interval,
    relative_threshold,
    absolute_threshold,
  )
  if comparison.get("decision") != expected_decision:
    raise ToolError("device report has an inconsistent comparison decision")


def _validate_raw_payload_schema(
  payload: Mapping[str, Any], *, phase: str
) -> None:
  required = {
    "schema_version",
    "kind",
    "device",
    "source_sha",
    "binary_sha256",
    "config_sha256",
    "config_file_sha256",
    "tool_sha256",
    "benchmark_source_sha256",
    "build_manifest_sha256",
    "toolchain",
    "collection",
    "environment",
    "observations",
  }
  expected_policy = "SHA-256 rank of seed/block/workload/A-or-B"
  if phase == "candidate":
    required |= {"threshold_manifest_sha256", "calibration_sha256"}
    expected_policy = "SHA-256 rank of seed/block/workload/backend"
  if set(payload) != required:
    raise ToolError("campaign payload has an incomplete schema")
  collection = payload.get("collection")
  if not isinstance(collection, dict) or set(collection) != {
    "seed",
    "repetitions",
    "minimum_time_seconds",
    "metric",
    "order_policy",
  }:
    raise ToolError("campaign payload has an incomplete collection schema")
  if (
    collection.get("metric") != "cpu_time_ns"
    or collection.get("order_policy") != expected_policy
  ):
    raise ToolError("campaign payload changed the collection schema")


def analyze_calibration(
  payload: Mapping[str, Any], config: Mapping[str, Any]
) -> dict[str, Any]:
  """Freeze per-device thresholds from a separate A/A campaign."""
  if (
    payload.get("schema_version") != 1
    or payload.get("kind") != "maintenance-aa-calibration"
  ):
    raise ToolError("threshold input is not an A/A calibration")
  _validate_raw_payload_schema(payload, phase="calibration")
  _validate_payload_identity(payload)
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
  environment = payload.get("environment")
  if not isinstance(environment, dict):
    raise ToolError("calibration has no environment identity")
  validate_device_environment(config, payload.get("device"), environment)
  build_manifest_sha256 = payload.get("build_manifest_sha256")
  if (
    not isinstance(build_manifest_sha256, str)
    or re.fullmatch(r"[0-9a-f]{64}", build_manifest_sha256) is None
  ):
    raise ToolError("calibration has no build-manifest identity")
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
  _validate_observation_order(
    observations,
    config,
    phase="calibration",
    seed=payload["collection"]["seed"],
    repetitions=repetitions,
  )
  _validate_observation_contexts(observations, payload)
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
      for entry in selected:
        _validate_work_counters(
          entry, config["workloads"][workload]["expected"]
        )
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
    "config_file_sha256": payload.get("config_file_sha256"),
    "benchmark_source_sha256": payload.get(
      "benchmark_source_sha256"
    ),
    "config_sha256": payload.get("config_sha256"),
    "toolchain": payload.get("toolchain"),
    "build_manifest_sha256": build_manifest_sha256,
    "environment": environment,
    "calibration_sha256": mapping_sha256(payload),
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
  _validate_raw_payload_schema(payload, phase="candidate")
  _validate_payload_identity(payload)
  _validate_threshold_manifest(thresholds, config)
  if not thresholds.get("valid"):
    raise ToolError("A/A noise exceeded the predeclared limit")
  if thresholds.get("analysis_policy") != config.get("analysis"):
    raise ToolError("threshold policy differs from the campaign config")
  for identity in (
    "device",
    "source_sha",
    "binary_sha256",
    "tool_sha256",
    "config_file_sha256",
    "benchmark_source_sha256",
    "config_sha256",
    "toolchain",
    "build_manifest_sha256",
    "environment",
  ):
    if payload.get(identity) != thresholds.get(identity):
      raise ToolError(f"candidate and calibration differ in {identity}")
  if payload.get("config_sha256") != mapping_sha256(config):
    raise ToolError("candidate config identity does not match policy")
  environment = payload.get("environment")
  if not isinstance(environment, dict):
    raise ToolError("candidate has no environment identity")
  validate_device_environment(config, payload.get("device"), environment)
  if payload.get("threshold_manifest_sha256") != mapping_sha256(thresholds):
    raise ToolError("candidate is not bound to this threshold manifest")
  if payload.get("calibration_sha256") != thresholds.get("calibration_sha256"):
    raise ToolError("candidate is not bound to this calibration")
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
  _validate_observation_order(
    observations,
    config,
    phase="candidate",
    seed=payload["collection"]["seed"],
    repetitions=repetitions,
  )
  _validate_observation_contexts(observations, payload)

  analysis = config["analysis"]
  workload_reports = {}
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
      for entry in selected:
        _validate_work_counters(
          entry, config["workloads"][workload]["expected"]
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
      absolute_low, absolute_high = _paired_interval(
        deltas,
        resamples=int(analysis["bootstrap_resamples"]),
        confidence=float(analysis["confidence"]),
        seed=int(analysis["bootstrap_seed"]) + 1_000 + comparison_index,
      )
      median_delta = statistics.median(deltas)
      decision = _classify_interval(
        (ci_low, ci_high),
        (absolute_low, absolute_high),
        relative_threshold,
        absolute_threshold,
      )
      comparisons[control] = {
        "control_median_ns": medians[control],
        "dirty_median_ns": medians["dirty_bit"],
        "median_relative_delta": statistics.median(ratios),
        "confidence_interval": [ci_low, ci_high],
        "median_absolute_delta_ns": median_delta,
        "absolute_confidence_interval_ns": [absolute_low, absolute_high],
        "decision": decision,
      }
    primary_control = analysis["primary_control"]
    decision = comparisons[primary_control]["decision"]
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
  primary_absolute_low, primary_absolute_high = _paired_interval(
    primary_deltas,
    resamples=int(analysis["bootstrap_resamples"]),
    confidence=float(analysis["confidence"]),
    seed=int(analysis["bootstrap_seed"]) + 11_000,
  )
  primary_delta = statistics.median(primary_deltas)
  primary_decision = _classify_interval(
    (primary_low, primary_high),
    (primary_absolute_low, primary_absolute_high),
    primary_relative_threshold,
    primary_absolute_threshold,
  )

  overall = _overall_decision(primary_decision, workload_reports, analysis)
  return {
    "schema_version": 1,
    "kind": "maintenance-device-report",
    "device": payload.get("device"),
    "source_sha": payload.get("source_sha"),
    "binary_sha256": payload.get("binary_sha256"),
    "tool_sha256": payload.get("tool_sha256"),
    "config_file_sha256": payload.get("config_file_sha256"),
    "benchmark_source_sha256": payload.get(
      "benchmark_source_sha256"
    ),
    "config_sha256": payload.get("config_sha256"),
    "toolchain": payload.get("toolchain"),
    "build_manifest_sha256": payload.get("build_manifest_sha256"),
    "environment": payload.get("environment"),
    "analysis_policy": analysis,
    "calibration_sha256": thresholds.get("calibration_sha256"),
    "threshold_manifest_sha256": mapping_sha256(thresholds),
    "primary_comparison": {
      "control": primary_control,
      "workloads": analysis["primary_workloads"],
      "median_relative_delta": statistics.median(primary_ratios),
      "confidence_interval": [primary_low, primary_high],
      "median_absolute_delta_ns": primary_delta,
      "absolute_confidence_interval_ns": [
        primary_absolute_low,
        primary_absolute_high,
      ],
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
  for report in reports:
    _validate_device_report(report, config)
  reports = sorted(reports, key=lambda report: report["device"])
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
  allowed = {"material_win", "material_regression", "flat", "inconclusive"}
  if any(decision not in allowed for decision in decisions):
    raise ToolError("device report contains an invalid decision")
  promote = "material_win" in decisions and all(
    decision in {"material_win", "flat"} for decision in decisions
  )
  return {
    "schema_version": 1,
    "source_sha": next(iter(source_shas)),
    "devices": [report.get("device") for report in reports],
    "device_decisions": decisions,
    "device_report_sha256": {
      report["device"]: mapping_sha256(report) for report in reports
    },
    "build_manifest_sha256": {
      report["device"]: report["build_manifest_sha256"] for report in reports
    },
    "decision": "promote" if promote else "keep_experimental",
  }


def _validate_device_report(
  report: Mapping[str, Any], config: Mapping[str, Any]
) -> None:
  required_report = {
    "schema_version",
    "kind",
    "device",
    "source_sha",
    "binary_sha256",
    "config_sha256",
    "config_file_sha256",
    "tool_sha256",
    "benchmark_source_sha256",
    "build_manifest_sha256",
    "toolchain",
    "environment",
    "analysis_policy",
    "calibration_sha256",
    "threshold_manifest_sha256",
    "primary_comparison",
    "overall_decision",
    "workloads",
  }
  if set(report) != required_report:
    raise ToolError("device report has an incomplete schema")
  if (
    report.get("schema_version") != 1
    or report.get("kind") != "maintenance-device-report"
  ):
    raise ToolError("cross-device input is not a device report")
  device = report.get("device")
  if device not in config["collection"]["devices"]:
    raise ToolError("device report does not name a frozen device")
  environment = report.get("environment")
  if not isinstance(environment, dict):
    raise ToolError("device report has no environment identity")
  validate_device_environment(config, device, environment)
  required_environment = {
    "system",
    "machine",
    "cpu_model",
    "logical_cpus",
    "python_version",
  }
  if not required_environment <= set(environment):
    raise ToolError("device report has an incomplete environment identity")
  if (
    not isinstance(environment["logical_cpus"], int)
    or isinstance(environment["logical_cpus"], bool)
    or environment["logical_cpus"] < 1
    or not isinstance(environment["python_version"], str)
    or not environment["python_version"]
  ):
    raise ToolError("device report has an invalid environment identity")
  if report.get("config_sha256") != mapping_sha256(config):
    raise ToolError("device report config identity does not match policy")
  if report.get("analysis_policy") != config["analysis"]:
    raise ToolError("device report analysis policy does not match config")
  if not isinstance(report.get("source_sha"), str) or re.fullmatch(
    r"[0-9a-f]{40}", report["source_sha"]
  ) is None:
    raise ToolError("device report has an invalid source SHA")
  for name in (
    "binary_sha256",
    "tool_sha256",
    "config_file_sha256",
    "benchmark_source_sha256",
    "build_manifest_sha256",
    "calibration_sha256",
    "threshold_manifest_sha256",
  ):
    if not isinstance(report.get(name), str) or re.fullmatch(
      r"[0-9a-f]{64}", report[name]
    ) is None:
      raise ToolError(f"device report has an invalid {name}")
  _validate_toolchain(report.get("toolchain"))
  workload_reports = report.get("workloads")
  if not isinstance(workload_reports, dict) or set(workload_reports) != set(
    config["workloads"]
  ):
    raise ToolError("device report has an incomplete workload set")
  allowed = {"material_win", "material_regression", "flat", "inconclusive"}
  for workload, workload_report in workload_reports.items():
    required_workload = {
      "primary_control",
      "relative_threshold",
      "absolute_threshold_ns",
      "decision",
      "medians_ns",
      "comparisons",
      "peak_rss_kib",
      "last_counters",
    }
    if (
      not isinstance(workload_report, dict)
      or set(workload_report) != required_workload
    ):
      raise ToolError(f"device report has an invalid {workload} result")
    relative_threshold = workload_report["relative_threshold"]
    absolute_threshold = workload_report["absolute_threshold_ns"]
    if (
      not _finite_number(relative_threshold)
      or float(relative_threshold) <= 0
      or not _finite_number(absolute_threshold)
      or float(absolute_threshold) <= 0
    ):
      raise ToolError(f"device report has invalid {workload} thresholds")
    comparisons = workload_report.get("comparisons")
    if not isinstance(comparisons, dict) or set(comparisons) != set(
      config["controls"]
    ):
      raise ToolError(f"device report has incomplete {workload} comparisons")
    if (
      workload_report.get("primary_control")
      != config["analysis"]["primary_control"]
    ):
      raise ToolError(f"device report changed the {workload} primary control")
    for comparison in comparisons.values():
      _validate_comparison_record(
        comparison,
        relative_threshold=float(relative_threshold),
        absolute_threshold=float(absolute_threshold),
      )
    if workload_report.get("decision") != comparisons[
      config["analysis"]["primary_control"]
    ]["decision"]:
      raise ToolError(f"device report has inconsistent {workload} decision")
    memory = workload_report.get("peak_rss_kib")
    counters = workload_report.get("last_counters")
    medians = workload_report.get("medians_ns")
    if not isinstance(medians, dict) or set(medians) != set(config["backends"]):
      raise ToolError(f"device report has incomplete {workload} medians")
    if any(
      not _finite_number(value) or value <= 0
      for value in medians.values()
    ):
      raise ToolError(f"device report has invalid {workload} medians")
    if not isinstance(memory, dict) or set(memory) != set(config["backends"]):
      raise ToolError(f"device report has incomplete {workload} memory")
    if any(
      not isinstance(value, int)
      or isinstance(value, bool)
      or value < 1
      for value in memory.values()
    ):
      raise ToolError(f"device report has invalid {workload} memory")
    if (
      not isinstance(counters, dict)
      or set(counters) != set(config["backends"])
    ):
      raise ToolError(f"device report has incomplete {workload} counters")
    for backend_counters in counters.values():
      _validate_work_counters(
        {"counters": backend_counters},
        config["workloads"][workload]["expected"],
      )
  primary = report.get("primary_comparison")
  required_primary = {
    "control",
    "workloads",
    "median_relative_delta",
    "confidence_interval",
    "median_absolute_delta_ns",
    "absolute_confidence_interval_ns",
    "relative_threshold",
    "absolute_threshold_ns",
    "decision",
  }
  if not isinstance(primary, dict) or set(primary) != required_primary:
    raise ToolError("device report has no primary comparison")
  if (
    primary.get("control") != config["analysis"]["primary_control"]
    or primary.get("workloads") != config["analysis"]["primary_workloads"]
    or primary.get("decision") not in allowed
  ):
    raise ToolError("device report has an invalid primary comparison")
  primary_relative_threshold = primary["relative_threshold"]
  primary_absolute_threshold = primary["absolute_threshold_ns"]
  if (
    not _finite_number(primary_relative_threshold)
    or primary_relative_threshold <= 0
    or not _finite_number(primary_absolute_threshold)
    or primary_absolute_threshold <= 0
    or not _finite_number(primary["median_relative_delta"])
    or not _finite_number(primary["median_absolute_delta_ns"])
  ):
    raise ToolError("device report has invalid primary measurements")
  relative_interval = _validate_interval(
    primary["confidence_interval"], name="primary interval"
  )
  absolute_interval = _validate_interval(
    primary["absolute_confidence_interval_ns"],
    name="absolute primary interval",
  )
  if primary["decision"] != _classify_interval(
    relative_interval,
    absolute_interval,
    float(primary_relative_threshold),
    float(primary_absolute_threshold),
  ):
    raise ToolError("device report has an inconsistent primary decision")
  expected_overall = _overall_decision(
    primary["decision"], workload_reports, config["analysis"]
  )
  if report.get("overall_decision") != expected_overall:
    raise ToolError("device report has an inconsistent overall decision")


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
  collect.add_argument("--build-manifest", type=Path, required=True)
  collect.add_argument("--thresholds", type=Path, required=True)
  collect.add_argument("--repetitions", type=int, default=30)
  collect.add_argument("--minimum-time", type=float, default=0.05)
  collect.add_argument("--seed", type=int, required=True)
  collect.add_argument("--output", type=Path, required=True)
  calibrate = subparsers.add_parser("calibrate")
  calibrate.add_argument("--binary", type=Path, required=True)
  calibrate.add_argument("--config", type=Path, required=True)
  calibrate.add_argument("--device", required=True)
  calibrate.add_argument("--build-manifest", type=Path, required=True)
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
  build_manifest = subparsers.add_parser("build-manifest")
  build_manifest.add_argument("--source-root", type=Path, required=True)
  build_manifest.add_argument("--source-sha", required=True)
  build_manifest.add_argument("--binary", type=Path, required=True)
  build_manifest.add_argument("--config", type=Path, required=True)
  build_manifest.add_argument("--compiler", type=Path, required=True)
  build_manifest.add_argument("--compile-commands", type=Path, required=True)
  build_manifest.add_argument("--link-command", type=Path, required=True)
  build_manifest.add_argument("--device", required=True)
  build_manifest.add_argument("--build-context", required=True)
  build_manifest.add_argument("--output", type=Path, required=True)
  return parser


def main(argv: Sequence[str] | None = None) -> int:
  args = _parser().parse_args(argv)
  try:
    if args.command == "toolchain":
      payload = _toolchain(args.compiler)
    elif args.command == "build-manifest":
      payload = create_build_manifest(
        source_root=args.source_root,
        source_sha=args.source_sha,
        binary=args.binary,
        config_path=args.config,
        compiler=args.compiler,
        compile_commands=args.compile_commands,
        link_command=args.link_command,
        device=args.device,
        build_context=args.build_context,
      )
    elif args.command == "collect":
      config = load_config(args.config)
      payload = collect_campaign(
        args.binary,
        config,
        device=args.device,
        config_file_sha256=_sha256(args.config),
        build_manifest=_read_json(args.build_manifest),
        thresholds=_read_json(args.thresholds),
        repetitions=args.repetitions,
        minimum_time_seconds=args.minimum_time,
        seed=args.seed,
      )
    elif args.command == "calibrate":
      config = load_config(args.config)
      payload = collect_calibration(
        args.binary,
        config,
        device=args.device,
        config_file_sha256=_sha256(args.config),
        build_manifest=_read_json(args.build_manifest),
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
