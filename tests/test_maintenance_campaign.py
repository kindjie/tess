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
    "counters": {
      "schedule_calls": 512,
      "executions": 1 if backend != "immediate" else 512,
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
  for order, entry in enumerate(observations):
    entry["order"] = order
  return {
    "schema_version": 1,
    "kind": "maintenance-candidate-campaign",
    "device": "test-device",
    "source_sha": "a" * 40,
    "binary_sha256": "b" * 64,
    "tool_sha256": "c" * 64,
    "config_sha256": CAMPAIGN.mapping_sha256(config) if config else None,
    "toolchain": {
      "name": "test-compiler",
      "sha256": "d" * 64,
      "version": "test compiler 1.0",
    },
    "collection": {
      "seed": 123,
      "repetitions": 4,
      "minimum_time_seconds": 0.01,
      "order_policy": "seeded shuffle of every workload/backend per round",
    },
    "environment": {"system": "TestOS", "machine": "test-arch"},
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
            }
          }
        },
        "collection": {
          "repetitions": 4,
          "minimum_time_seconds": 0.01,
          "devices": {
            "test-device": {
              "calibration_seed": 122,
              "candidate_seed": 123,
            },
            "second-test-device": {
              "calibration_seed": 124,
              "candidate_seed": 125,
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
  for order, entry in enumerate(observations):
    entry["order"] = order
  return {
    "schema_version": 1,
    "kind": "maintenance-aa-calibration",
    "device": "test-device",
    "source_sha": "a" * 40,
    "binary_sha256": "b" * 64,
    "tool_sha256": "c" * 64,
    "config_sha256": CAMPAIGN.mapping_sha256(config) if config else None,
    "toolchain": {
      "name": "test-compiler",
      "sha256": "d" * 64,
      "version": "test compiler 1.0",
    },
    "collection": {
      "seed": 122,
      "repetitions": 4,
      "minimum_time_seconds": 0.01,
    },
    "observations": observations,
  }


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
  report = CAMPAIGN.analyze_campaign(
    campaign_payload([10_000.0, 10_100.0, 9_900.0, 10_050.0], config),
    config,
    CAMPAIGN.analyze_calibration(
      calibration_payload(config), config
    ),
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
  report = CAMPAIGN.analyze_campaign(
    campaign_payload([22_000.0, 22_200.0, 21_800.0, 22_100.0], config),
    config,
    CAMPAIGN.analyze_calibration(
      calibration_payload(config), config
    ),
  )

  assert report["workloads"]["dense"]["decision"] == "material_regression"


def test_summary_fails_closed_on_missing_repetition(tmp_path: Path):
  config_path = tmp_path / "config.json"
  write_config(config_path)
  config = CAMPAIGN.load_config(config_path)
  payload = campaign_payload(
    [10_000.0, 10_100.0, 9_900.0, 10_050.0], config
  )
  payload["observations"].pop()

  with pytest.raises(CAMPAIGN.ToolError, match="missing observations"):
    CAMPAIGN.analyze_campaign(
      payload,
      config,
      CAMPAIGN.analyze_calibration(
        calibration_payload(config), config
      ),
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
  payload["kind"] = "unrelated-results"

  with pytest.raises(CAMPAIGN.ToolError, match="candidate campaign"):
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


def test_cross_device_decision_needs_win_without_regression():
  config = {
    "collection": {"devices": {"m3": {}, "steam-deck": {}}},
  }
  config_sha = CAMPAIGN.mapping_sha256(config)

  def report(device: str, decision: str) -> dict:
    return {
      "schema_version": 1,
      "kind": "maintenance-device-report",
      "device": device,
      "source_sha": "a" * 40,
      "tool_sha256": "b" * 64,
      "config_sha256": config_sha,
      "overall_decision": decision,
    }

  win = report("m3", "material_win")
  flat = report("steam-deck", "flat")
  regression = report("steam-deck", "material_regression")

  assert (
    CAMPAIGN.cross_device_decision([win, flat], config)["decision"]
    == "promote"
  )
  assert (
    CAMPAIGN.cross_device_decision([win, regression], config)["decision"]
    == "keep_experimental"
  )


def test_cross_device_decision_rejects_wrong_device_set():
  config = {
    "collection": {"devices": {"m3": {}, "steam-deck": {}}},
  }
  report = {
    "schema_version": 1,
    "kind": "maintenance-device-report",
    "device": "unexpected-device",
    "source_sha": "a" * 40,
    "tool_sha256": "b" * 64,
    "config_sha256": CAMPAIGN.mapping_sha256(config),
    "overall_decision": "material_win",
  }

  with pytest.raises(CAMPAIGN.ToolError, match="frozen device set"):
    CAMPAIGN.cross_device_decision(
      [report, {**report, "device": "m3"}], config
    )
