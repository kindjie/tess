"""Tests for the paired maintenance promotion campaign."""

import importlib.util
import json
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
  "maintenance_campaign", ROOT / "tools" / "maintenance_campaign.py"
)
assert SPEC is not None and SPEC.loader is not None
CAMPAIGN = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CAMPAIGN)

SOURCE_SHA = "a" * 40
CONFIG_FILE_SHA = "2" * 64
TOOL_SHA = "c" * 64
BENCHMARK_SOURCE_SHA = "3" * 64


def observation(backend: str, repetition: int, cpu_time_ns: float) -> dict:
  return {
    "workload": "dense",
    "backend": backend,
    "repetition": repetition,
    "order": repetition * 4,
    "cpu_time_ns": cpu_time_ns,
    "real_time_ns": cpu_time_ns,
    "iterations": 100,
    "peak_rss_kib": 1024,
    "stderr": "",
    "benchmark_context": {
      "num_cpus": 8,
      "cpu_scaling_enabled": False,
      "library_version": "test",
      "library_build_type": "release",
      "json_schema_version": 1,
      "tess_source_sha": SOURCE_SHA,
      "tess_config_file_sha256": CONFIG_FILE_SHA,
      "tess_tool_sha256": TOOL_SHA,
      "tess_benchmark_source_sha256": BENCHMARK_SOURCE_SHA,
    },
    "counters": {
      "offers": 1,
      "active_tasks": 1,
      "resident_slots": 1,
      "rebuilds": 1,
      "drain_calls": 1,
      "schedule_calls": 1,
      "coalesced_calls": 0,
      "executions": 1,
      "capacity_failures": 0,
      "scanned_values": 16,
      "authoritative_checksum": 1,
    },
  }


def campaign_payload(
  dirty_times: list[float], config: dict | None = None
) -> dict:
  observations = []
  control_times = {
    "immediate": [20_000.0, 20_200.0, 19_800.0, 20_100.0],
    "fifo": [30_000.0, 30_300.0, 29_700.0, 30_100.0],
    "coalescing": [15_000.0, 15_100.0, 14_900.0, 15_050.0],
  }
  for backend, times in control_times.items():
    observations.extend(
      observation(backend, repetition, value)
      for repetition, value in enumerate(times)
    )
  observations.extend(
    observation("dirty_bit", repetition, value)
    for repetition, value in enumerate(dirty_times)
  )
  if config is not None:
    lookup = {
      (entry["workload"], entry["backend"], entry["repetition"]): entry
      for entry in observations
    }
    observations = [
      lookup[(workload, backend, repetition)]
      for repetition in range(4)
      for workload, backend, _ in CAMPAIGN._candidate_schedule(
        config, 123, repetition
      )
    ]
  for order, entry in enumerate(observations):
    entry["order"] = order
  return {
    "schema_version": 1,
    "kind": "maintenance-candidate-campaign",
    "device": "test-device",
    "source_sha": SOURCE_SHA,
    "binary_sha256": "b" * 64,
    "tool_sha256": TOOL_SHA,
    "config_file_sha256": CONFIG_FILE_SHA,
    "benchmark_source_sha256": BENCHMARK_SOURCE_SHA,
    "config_sha256": CAMPAIGN.mapping_sha256(config) if config else None,
    "build_manifest_sha256": "e" * 64,
    "toolchain": {
      "name": "test-compiler",
      "sha256": "d" * 64,
      "version": "test compiler 1.0",
    },
    "collection": {
      "seed": 123,
      "repetitions": 4,
      "minimum_time_seconds": 0.01,
      "metric": "cpu_time_ns",
      "order_policy": "SHA-256 rank of seed/block/workload/backend",
    },
    "environment": {
      "system": "TestOS",
      "machine": "test-arch",
      "cpu_model": "TestCPU",
      "logical_cpus": 8,
      "python_version": "3.14.0",
    },
    "observations": observations,
  }


def write_config(path: Path) -> None:
  path.write_text(
    json.dumps(
      {
        "schema_version": 1,
        "backends": ["immediate", "fifo", "coalescing", "dirty_bit"],
        "controls": ["immediate", "fifo", "coalescing"],
        "workloads": {
          "dense": {
            "benchmarks": {
              backend: f"maintenance/campaign/dense/{backend}"
              for backend in (
                "immediate",
                "fifo",
                "coalescing",
                "dirty_bit",
              )
            },
            "expected": {
              "registered_tasks": 1,
              "active_tasks": 1,
              "offers": 1,
            },
          }
        },
        "collection": {
          "repetitions": 4,
          "minimum_time_seconds": 0.01,
          "devices": {
            "test-device": {
              "calibration_seed": 122,
              "candidate_seed": 123,
              "build_context": "test-build",
              "environment": {
                "system": "TestOS",
                "machine": "test-arch",
                "cpu_model_pattern": "^TestCPU$",
              },
            },
            "second-test-device": {
              "calibration_seed": 124,
              "candidate_seed": 125,
              "build_context": "second-test-build",
              "environment": {
                "system": "SecondOS",
                "machine": "second-arch",
                "cpu_model_pattern": "^SecondCPU$",
              },
            }
          },
        },
        "analysis": {
          "confidence": 0.95,
          "bootstrap_resamples": 2_000,
          "bootstrap_seed": 456,
          "minimum_relative_effect": 0.08,
          "minimum_absolute_effect_ns": 500.0,
          "noise_multiplier": 2.0,
          "maximum_relative_noise": 0.10,
          "calibration_backend": "coalescing",
          "primary_control": "coalescing",
          "primary_workloads": ["dense"],
          "guardrail_controls": ["immediate", "coalescing"],
        },
      }
    ),
    encoding="utf-8",
  )


def calibration_payload(config: dict | None = None) -> dict:
  observations = []
  for repetition, (first, second) in enumerate(
    ((15_000.0, 15_050.0), (15_100.0, 15_000.0),
     (14_900.0, 15_000.0), (15_050.0, 15_100.0))
  ):
    for side, value in (("a", first), ("b", second)):
      entry = observation("coalescing", repetition, value)
      entry["side"] = side
      observations.append(entry)
  if config is not None:
    lookup = {
      (
        entry["workload"],
        entry["side"],
        entry["repetition"],
      ): entry
      for entry in observations
    }
    observations = [
      lookup[(workload, side, repetition)]
      for repetition in range(4)
      for workload, side, _ in CAMPAIGN._calibration_schedule(
        config, 122, repetition
      )
    ]
  for order, entry in enumerate(observations):
    entry["order"] = order
  return {
    "schema_version": 1,
    "kind": "maintenance-aa-calibration",
    "device": "test-device",
    "source_sha": SOURCE_SHA,
    "binary_sha256": "b" * 64,
    "tool_sha256": TOOL_SHA,
    "config_file_sha256": CONFIG_FILE_SHA,
    "benchmark_source_sha256": BENCHMARK_SOURCE_SHA,
    "config_sha256": CAMPAIGN.mapping_sha256(config) if config else None,
    "build_manifest_sha256": "e" * 64,
    "toolchain": {
      "name": "test-compiler",
      "sha256": "d" * 64,
      "version": "test compiler 1.0",
    },
    "collection": {
      "seed": 122,
      "repetitions": 4,
      "minimum_time_seconds": 0.01,
      "metric": "cpu_time_ns",
      "order_policy": "SHA-256 rank of seed/block/workload/A-or-B",
    },
    "environment": {
      "system": "TestOS",
      "machine": "test-arch",
      "cpu_model": "TestCPU",
      "logical_cpus": 8,
      "python_version": "3.14.0",
    },
    "observations": observations,
  }


def bind_candidate(payload: dict, thresholds: dict) -> dict:
  payload["threshold_manifest_sha256"] = CAMPAIGN.mapping_sha256(thresholds)
  payload["calibration_sha256"] = thresholds["calibration_sha256"]
  return payload


def test_embedded_identity_rejects_stale_or_missing_binary_context():
  identity = {
    "source_sha": SOURCE_SHA,
    "config_file_sha256": CONFIG_FILE_SHA,
    "tool_sha256": TOOL_SHA,
    "benchmark_source_sha256": BENCHMARK_SOURCE_SHA,
  }
  context = observation("dirty_bit", 0, 10_000.0)["benchmark_context"]

  CAMPAIGN._validate_embedded_identity(context, identity)

  missing = dict(context)
  missing.pop("tess_tool_sha256")
  with pytest.raises(CAMPAIGN.ToolError, match="embedded identity"):
    CAMPAIGN._validate_embedded_identity(missing, identity)

  stale = dict(context)
  stale["tess_source_sha"] = "f" * 40
  with pytest.raises(CAMPAIGN.ToolError, match="embedded identity"):
    CAMPAIGN._validate_embedded_identity(stale, identity)


def test_calibration_freezes_noise_derived_thresholds(tmp_path: Path):
  config_path = tmp_path / "config.json"
  write_config(config_path)
  config = CAMPAIGN.load_config(config_path)
  thresholds = CAMPAIGN.analyze_calibration(
    calibration_payload(config), config
  )

  dense = thresholds["workloads"]["dense"]
  assert dense["valid"] is True
  assert dense["relative_threshold"] >= 0.08
  assert dense["absolute_threshold_ns"] >= 500.0


def test_summary_uses_predeclared_control_and_material_win(tmp_path: Path):
  config_path = tmp_path / "config.json"
  write_config(config_path)
  config = CAMPAIGN.load_config(config_path)
  thresholds = CAMPAIGN.analyze_calibration(
    calibration_payload(config), config
  )
  report = CAMPAIGN.analyze_campaign(
    bind_candidate(
      campaign_payload([10_000.0, 10_100.0, 9_900.0, 10_050.0], config),
      thresholds,
    ),
    config,
    thresholds,
  )

  result = report["workloads"]["dense"]
  assert result["primary_control"] == "coalescing"
  assert result["decision"] == "material_win"
  assert result["relative_threshold"] >= 0.08
  assert result["absolute_threshold_ns"] >= 500.0
  assert set(result["comparisons"]) == {
    "immediate",
    "fifo",
    "coalescing",
  }


def test_summary_reports_material_regression(tmp_path: Path):
  config_path = tmp_path / "config.json"
  write_config(config_path)
  config = CAMPAIGN.load_config(config_path)
  thresholds = CAMPAIGN.analyze_calibration(
    calibration_payload(config), config
  )
  report = CAMPAIGN.analyze_campaign(
    bind_candidate(
      campaign_payload([22_000.0, 22_200.0, 21_800.0, 22_100.0], config),
      thresholds,
    ),
    config,
    thresholds,
  )

  assert report["workloads"]["dense"]["decision"] == "material_regression"


def test_primary_aggregate_regression_cannot_be_overridden(tmp_path: Path):
  config_path = tmp_path / "config.json"
  write_config(config_path)
  raw_config = json.loads(config_path.read_text(encoding="utf-8"))
  raw_config["analysis"]["guardrail_controls"] = ["immediate"]
  config_path.write_text(json.dumps(raw_config), encoding="utf-8")
  config = CAMPAIGN.load_config(config_path)
  thresholds = CAMPAIGN.analyze_calibration(
    calibration_payload(config), config
  )
  payload = bind_candidate(
    campaign_payload([17_000.0, 17_100.0, 16_900.0, 17_050.0], config),
    thresholds,
  )

  report = CAMPAIGN.analyze_campaign(payload, config, thresholds)

  assert report["primary_comparison"]["decision"] == "material_regression"
  assert report["overall_decision"] == "material_regression"


def test_uncertain_regression_boundary_is_inconclusive(tmp_path: Path):
  config_path = tmp_path / "config.json"
  write_config(config_path)
  config = CAMPAIGN.load_config(config_path)
  thresholds = CAMPAIGN.analyze_calibration(
    calibration_payload(config), config
  )
  payload = bind_candidate(
    campaign_payload([12_000.0, 18_000.0, 12_000.0, 18_000.0], config),
    thresholds,
  )

  report = CAMPAIGN.analyze_campaign(payload, config, thresholds)

  assert report["primary_comparison"]["decision"] == "inconclusive"
  assert report["overall_decision"] == "inconclusive"


def test_summary_fails_closed_on_missing_repetition(tmp_path: Path):
  config_path = tmp_path / "config.json"
  write_config(config_path)
  config = CAMPAIGN.load_config(config_path)
  payload = campaign_payload(
    [10_000.0, 10_100.0, 9_900.0, 10_050.0], config
  )
  payload["observations"].pop()
  thresholds = CAMPAIGN.analyze_calibration(
    calibration_payload(config), config
  )
  bind_candidate(payload, thresholds)

  with pytest.raises(CAMPAIGN.ToolError, match="missing observations"):
    CAMPAIGN.analyze_campaign(
      payload,
      config,
      thresholds,
    )


def test_summary_rejects_stale_config_identity(tmp_path: Path):
  config_path = tmp_path / "config.json"
  write_config(config_path)
  config = CAMPAIGN.load_config(config_path)
  thresholds = CAMPAIGN.analyze_calibration(
    calibration_payload(config), config
  )
  payload = campaign_payload(
    [10_000.0, 10_100.0, 9_900.0, 10_050.0], config
  )
  bind_candidate(payload, thresholds)
  payload["config_sha256"] = "d" * 64

  with pytest.raises(CAMPAIGN.ToolError, match="config_sha256"):
    CAMPAIGN.analyze_campaign(payload, config, thresholds)


def test_summary_rejects_wrong_payload_kind(tmp_path: Path):
  config_path = tmp_path / "config.json"
  write_config(config_path)
  config = CAMPAIGN.load_config(config_path)
  thresholds = CAMPAIGN.analyze_calibration(
    calibration_payload(config), config
  )
  payload = campaign_payload(
    [10_000.0, 10_100.0, 9_900.0, 10_050.0], config
  )
  bind_candidate(payload, thresholds)
  payload["kind"] = "unrelated-results"

  with pytest.raises(CAMPAIGN.ToolError, match="candidate campaign"):
    CAMPAIGN.analyze_campaign(payload, config, thresholds)


def test_summary_rejects_incomplete_collection_schema(tmp_path: Path):
  config_path = tmp_path / "config.json"
  write_config(config_path)
  config = CAMPAIGN.load_config(config_path)
  thresholds = CAMPAIGN.analyze_calibration(
    calibration_payload(config), config
  )
  payload = bind_candidate(
    campaign_payload([10_000.0, 10_100.0, 9_900.0, 10_050.0], config),
    thresholds,
  )
  payload["collection"].pop("order_policy")

  with pytest.raises(CAMPAIGN.ToolError, match="collection schema"):
    CAMPAIGN.analyze_campaign(payload, config, thresholds)


def test_summary_rejects_unbound_thresholds(tmp_path: Path):
  config_path = tmp_path / "config.json"
  write_config(config_path)
  config = CAMPAIGN.load_config(config_path)
  thresholds = CAMPAIGN.analyze_calibration(
    calibration_payload(config), config
  )
  payload = bind_candidate(
    campaign_payload([10_000.0, 10_100.0, 9_900.0, 10_050.0], config),
    thresholds,
  )
  payload["threshold_manifest_sha256"] = "0" * 64

  with pytest.raises(CAMPAIGN.ToolError, match="not bound"):
    CAMPAIGN.analyze_campaign(payload, config, thresholds)

  malformed = dict(thresholds)
  malformed.pop("benchmark_source_sha256")
  with pytest.raises(CAMPAIGN.ToolError, match="threshold manifest"):
    CAMPAIGN.analyze_campaign(payload, config, malformed)


def test_summary_rejects_wrong_hardware_and_incomplete_raw_data(
  tmp_path: Path,
):
  config_path = tmp_path / "config.json"
  write_config(config_path)
  config = CAMPAIGN.load_config(config_path)
  thresholds = CAMPAIGN.analyze_calibration(
    calibration_payload(config), config
  )
  payload = bind_candidate(
    campaign_payload([10_000.0, 10_100.0, 9_900.0, 10_050.0], config),
    thresholds,
  )
  payload["environment"]["cpu_model"] = "WrongCPU"
  with pytest.raises(CAMPAIGN.ToolError, match="environment"):
    CAMPAIGN.analyze_campaign(payload, config, thresholds)

  payload = bind_candidate(
    campaign_payload([10_000.0, 10_100.0, 9_900.0, 10_050.0], config),
    thresholds,
  )
  payload["observations"][0]["counters"].pop("resident_slots")
  with pytest.raises(CAMPAIGN.ToolError, match="work counters"):
    CAMPAIGN.analyze_campaign(payload, config, thresholds)

  payload = bind_candidate(
    campaign_payload([10_000.0, 10_100.0, 9_900.0, 10_050.0], config),
    thresholds,
  )
  payload["observations"][0]["peak_rss_kib"] = None
  with pytest.raises(CAMPAIGN.ToolError, match="RSS"):
    CAMPAIGN.analyze_campaign(payload, config, thresholds)

  payload = bind_candidate(
    campaign_payload([10_000.0, 10_100.0, 9_900.0, 10_050.0], config),
    thresholds,
  )
  payload["observations"][0]["benchmark_context"]["num_cpus"] = 7
  with pytest.raises(CAMPAIGN.ToolError, match="context drift"):
    CAMPAIGN.analyze_campaign(payload, config, thresholds)

  payload = bind_candidate(
    campaign_payload([10_000.0, 10_100.0, 9_900.0, 10_050.0], config),
    thresholds,
  )
  payload["observations"][0]["benchmark_context"][
    "cpu_scaling_enabled"
  ] = True
  with pytest.raises(CAMPAIGN.ToolError, match="frequency scaling"):
    CAMPAIGN.analyze_campaign(payload, config, thresholds)


def test_summary_reconstructs_exact_collection_schedule(tmp_path: Path):
  config_path = tmp_path / "config.json"
  write_config(config_path)
  config = CAMPAIGN.load_config(config_path)
  thresholds = CAMPAIGN.analyze_calibration(
    calibration_payload(config), config
  )
  payload = bind_candidate(
    campaign_payload([10_000.0, 10_100.0, 9_900.0, 10_050.0], config),
    thresholds,
  )
  payload["observations"][0]["backend"], payload["observations"][1][
    "backend"
  ] = (
    payload["observations"][1]["backend"],
    payload["observations"][0]["backend"],
  )

  with pytest.raises(CAMPAIGN.ToolError, match="collection schedule"):
    CAMPAIGN.analyze_campaign(payload, config, thresholds)


def test_collection_rejects_unplanned_parameters(tmp_path: Path):
  config_path = tmp_path / "config.json"
  write_config(config_path)
  config = CAMPAIGN.load_config(config_path)

  with pytest.raises(CAMPAIGN.ToolError, match="candidate seed"):
    CAMPAIGN.validate_collection_parameters(
      config,
      device="test-device",
      phase="candidate",
      repetitions=4,
      minimum_time_seconds=0.01,
      seed=999,
    )


def test_toolchain_manifest_rejects_incomplete_identity(tmp_path: Path):
  manifest = tmp_path / "toolchain.json"
  manifest.write_text(
    json.dumps(
      {
        "name": "compiler",
        "sha256": "not-a-digest",
        "version": "compiler 1.0",
      }
    ),
    encoding="utf-8",
  )

  with pytest.raises(CAMPAIGN.ToolError, match="toolchain identity"):
    CAMPAIGN._load_toolchain_manifest(manifest)


def test_build_manifest_binds_relative_compile_entry_and_binary_context(
  tmp_path: Path, monkeypatch: pytest.MonkeyPatch
):
  source_root = tmp_path / "source"
  benchmark_dir = source_root / "bench"
  build_root = source_root / "build" / "bench-only"
  benchmark_dir.mkdir(parents=True)
  build_root.mkdir(parents=True)
  benchmark_source = benchmark_dir / "tess_maintenance_campaign_bench.cc"
  benchmark_source.write_text("// test source\n", encoding="utf-8")
  config_path = source_root / "config.json"
  write_config(config_path)
  binary = build_root / "campaign"
  binary.write_bytes(b"test binary")
  compiler = tmp_path / "compiler"
  compiler.write_bytes(b"test compiler")
  compile_commands = build_root / "compile_commands.json"
  compile_commands.write_text(
    json.dumps(
      [
        {
          "directory": str(build_root),
          "file": "../../bench/tess_maintenance_campaign_bench.cc",
          "arguments": [str(compiler), "-O3", str(benchmark_source)],
        }
      ]
    ),
    encoding="utf-8",
  )
  link_command = build_root / "link.txt"
  link_command.write_text(
    f"{compiler} -O3 {build_root}/campaign.o -o {binary}\n",
    encoding="utf-8",
  )
  monkeypatch.setattr(
    CAMPAIGN,
    "_run_git",
    lambda _root, *arguments: SOURCE_SHA if arguments[0] == "rev-parse" else "",
  )
  monkeypatch.setattr(
    CAMPAIGN,
    "_toolchain",
    lambda _compiler: {
      "name": "test-compiler",
      "sha256": "4" * 64,
      "version": "test compiler 1.0",
    },
  )
  identity = {
    "tess_source_sha": SOURCE_SHA,
    "tess_config_file_sha256": CAMPAIGN._sha256(config_path),
    "tess_tool_sha256": CAMPAIGN._sha256(Path(CAMPAIGN.__file__)),
    "tess_benchmark_source_sha256": CAMPAIGN._sha256(benchmark_source),
  }
  monkeypatch.setattr(
    CAMPAIGN,
    "_one_observation",
    lambda *_args: {"benchmark_context": identity},
  )

  manifest = CAMPAIGN.create_build_manifest(
    source_root=source_root,
    source_sha=SOURCE_SHA,
    binary=binary,
    config_path=config_path,
    compiler=compiler,
    compile_commands=compile_commands,
    link_command=link_command,
    device="test-device",
    build_context="test-build",
  )

  assert manifest["embedded_identity"]["source_sha"] == SOURCE_SHA
  assert str(tmp_path) not in manifest["compile_command"]["text"]
  assert "$SOURCE" in manifest["compile_command"]["text"]

  identity["tess_source_sha"] = "f" * 40
  with pytest.raises(CAMPAIGN.ToolError, match="embedded identity"):
    CAMPAIGN.create_build_manifest(
      source_root=source_root,
      source_sha=SOURCE_SHA,
      binary=binary,
      config_path=config_path,
      compiler=compiler,
      compile_commands=compile_commands,
      link_command=link_command,
      device="test-device",
      build_context="test-build",
    )


def device_report(config: dict, device: str, decision: str) -> dict:
  environment_policy = config["collection"]["devices"][device]["environment"]
  cpu_model = (
    "TestCPU" if device == "test-device" else "SecondCPU"
  )
  comparisons = {
    control: {
      "control_median_ns": 15_000.0,
      "dirty_median_ns": 15_000.0,
      "median_relative_delta": 0.0,
      "confidence_interval": [-0.01, 0.01],
      "median_absolute_delta_ns": 0.0,
      "absolute_confidence_interval_ns": [-100.0, 100.0],
      "decision": "flat",
    }
    for control in config["controls"]
  }
  last_counters = {
    "offers": 1,
    "active_tasks": 1,
    "resident_slots": 1,
    "rebuilds": 1,
    "drain_calls": 1,
    "schedule_calls": 1,
    "coalesced_calls": 0,
    "executions": 1,
    "capacity_failures": 0,
    "scanned_values": 16,
    "authoritative_checksum": 1,
  }
  workload = {
    "primary_control": config["analysis"]["primary_control"],
    "relative_threshold": 0.08,
    "absolute_threshold_ns": 500.0,
    "decision": "flat",
    "medians_ns": {backend: 15_000.0 for backend in config["backends"]},
    "comparisons": comparisons,
    "peak_rss_kib": {backend: 1024 for backend in config["backends"]},
    "last_counters": {
      backend: dict(last_counters) for backend in config["backends"]
    },
  }
  primary_intervals = {
    "material_win": ([-0.20, -0.10], [-2_000.0, -1_000.0]),
    "material_regression": ([0.10, 0.20], [1_000.0, 2_000.0]),
    "inconclusive": ([0.00, 0.10], [0.0, 1_000.0]),
    "flat": ([-0.01, 0.01], [-100.0, 100.0]),
  }
  relative_interval, absolute_interval = primary_intervals[decision]
  return {
    "schema_version": 1,
    "kind": "maintenance-device-report",
    "device": device,
    "source_sha": SOURCE_SHA,
    "binary_sha256": "b" * 64,
    "tool_sha256": TOOL_SHA,
    "config_file_sha256": CONFIG_FILE_SHA,
    "benchmark_source_sha256": BENCHMARK_SOURCE_SHA,
    "config_sha256": CAMPAIGN.mapping_sha256(config),
    "build_manifest_sha256": "d" * 64,
    "calibration_sha256": "e" * 64,
    "threshold_manifest_sha256": "f" * 64,
    "toolchain": {
      "name": "test-compiler",
      "sha256": "1" * 64,
      "version": "test compiler 1.0",
    },
    "environment": {
      "system": environment_policy["system"],
      "machine": environment_policy["machine"],
      "cpu_model": cpu_model,
      "logical_cpus": 8,
      "python_version": "3.14.0",
    },
    "analysis_policy": config["analysis"],
    "primary_comparison": {
      "control": config["analysis"]["primary_control"],
      "workloads": config["analysis"]["primary_workloads"],
      "median_relative_delta": 0.0,
      "confidence_interval": relative_interval,
      "median_absolute_delta_ns": 0.0,
      "absolute_confidence_interval_ns": absolute_interval,
      "relative_threshold": 0.08,
      "absolute_threshold_ns": 500.0,
      "decision": decision,
    },
    "overall_decision": decision,
    "workloads": {"dense": workload},
  }


def test_cross_device_decision_needs_win_without_regression(tmp_path: Path):
  config_path = tmp_path / "config.json"
  write_config(config_path)
  config = CAMPAIGN.load_config(config_path)
  win = device_report(config, "test-device", "material_win")
  flat = device_report(config, "second-test-device", "flat")
  regression = device_report(
    config, "second-test-device", "material_regression"
  )
  inconclusive = device_report(
    config, "second-test-device", "inconclusive"
  )

  assert (
    CAMPAIGN.cross_device_decision([win, flat], config)["decision"]
    == "promote"
  )
  assert (
    CAMPAIGN.cross_device_decision([win, regression], config)["decision"]
    == "keep_experimental"
  )
  assert (
    CAMPAIGN.cross_device_decision([win, inconclusive], config)["decision"]
    == "keep_experimental"
  )


def test_cross_device_decision_rejects_wrong_device_set(tmp_path: Path):
  config_path = tmp_path / "config.json"
  write_config(config_path)
  config = CAMPAIGN.load_config(config_path)
  report = device_report(config, "test-device", "material_win")
  report["device"] = "unexpected-device"

  with pytest.raises(CAMPAIGN.ToolError, match="frozen device"):
    CAMPAIGN.cross_device_decision(
      [report, device_report(config, "test-device", "flat")], config
    )


def test_cross_device_decision_rejects_skeletal_report(tmp_path: Path):
  config_path = tmp_path / "config.json"
  write_config(config_path)
  config = CAMPAIGN.load_config(config_path)
  first = device_report(config, "test-device", "material_win")
  second = device_report(config, "second-test-device", "flat")
  first["workloads"]["dense"]["comparisons"]["coalescing"].pop(
    "confidence_interval"
  )

  with pytest.raises(CAMPAIGN.ToolError, match="comparison"):
    CAMPAIGN.cross_device_decision([first, second], config)


def test_cross_device_decision_rejects_incomplete_report_counters(
  tmp_path: Path,
):
  config_path = tmp_path / "config.json"
  write_config(config_path)
  config = CAMPAIGN.load_config(config_path)
  first = device_report(config, "test-device", "material_win")
  second = device_report(config, "second-test-device", "flat")
  first["workloads"]["dense"]["last_counters"]["dirty_bit"].pop(
    "resident_slots"
  )

  with pytest.raises(CAMPAIGN.ToolError, match="work counters"):
    CAMPAIGN.cross_device_decision([first, second], config)
