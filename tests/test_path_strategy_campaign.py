"""Focused tests for the path-strategy campaign runner."""

import importlib.util
import json
from pathlib import Path
from types import SimpleNamespace

import pytest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
  "path_strategy_campaign", ROOT / "tools" / "path_strategy_campaign.py"
)
assert SPEC and SPEC.loader
CAMPAIGN = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CAMPAIGN)


def test_exact_name_anchors_one_registration():
  """The generated filter identity includes every workload dimension."""
  assert CAMPAIGN.exact_name("unit_shared/open", "astar", 512, 100) == (
    "lab/path_strategy_crossover/unit_shared/open/astar/512x512/100"
  )


def test_summary_reports_distribution_not_only_median():
  """Published cells retain spread as well as central tendency."""
  summary = CAMPAIGN.summarize_samples([9.0, 10.0, 11.0, 12.0])
  assert summary["median_cpu_ns"] == 10.5
  assert summary["q1_cpu_ns"] == 9.75
  assert summary["q3_cpu_ns"] == 11.25
  assert summary["p95_cpu_ns"] == pytest.approx(11.85)
  assert summary["coefficient_of_variation"] > 0


def test_summary_rejects_empty_samples():
  """Missing measurements cannot silently become evidence."""
  with pytest.raises(CAMPAIGN.CampaignError, match="empty"):
    CAMPAIGN.summarize_samples([])


def test_paired_near_tie_is_inconclusive():
  """Spread crossing the effect floor cannot manufacture a winner."""
  result = CAMPAIGN.classify_paired_ratios([0.97, 0.99, 1.0, 1.01], 0.02)
  assert result["verdict"] == "inconclusive"


def test_paired_interval_requires_a_practical_win():
  """A consistently material speedup can support the right-arm verdict."""
  result = CAMPAIGN.classify_paired_ratios([0.80, 0.82, 0.84, 0.86], 0.02)
  assert result["verdict"] == "right_wins"


def test_crossover_is_a_stable_bracket_and_reports_reversals():
  """A later reversal prevents a premature universal threshold."""
  rows = [
    {
      "count": 8,
      "paired": {"verdict": "left_wins"},
    },
    {
      "count": 16,
      "paired": {"verdict": "right_wins"},
    },
    {
      "count": 32,
      "paired": {"verdict": "left_wins"},
    },
    {
      "count": 64,
      "paired": {"verdict": "right_wins"},
    },
  ]
  result = CAMPAIGN.crossover_analysis(rows)
  assert result["stable_right_win_bracket"] == {
    "lower_exclusive": 32,
    "upper_inclusive": 64,
  }
  assert len(result["transitions"]) == 3


def test_stable_counters_excludes_timing_and_process_peak():
  """Only deterministic work counters participate in repetition checks."""
  assert CAMPAIGN.stable_counters({
    "requests": 8,
    "items_per_second": 123.0,
    "process.peak_rss_bytes": 4096,
  }) == {"requests": 8}


def test_interrupted_monitor_kills_and_waits_for_child(monkeypatch, tmp_path):
  """Parent interruption cannot orphan a capacity-seeking child process."""

  class FakeProcess:
    pid = 4242

    def __init__(self):
      self.waited = False

    def poll(self):
      return None

    def wait(self):
      self.waited = True

  process = FakeProcess()
  killed = []
  monkeypatch.setattr(
    CAMPAIGN.subprocess, "Popen", lambda *args, **kwargs: process
  )
  monkeypatch.setattr(
    CAMPAIGN,
    "child_rss_bytes",
    lambda _pid: (_ for _ in ()).throw(KeyboardInterrupt()),
  )
  monkeypatch.setattr(
    CAMPAIGN.os, "killpg", lambda pid, sig: killed.append((pid, sig))
  )

  with pytest.raises(KeyboardInterrupt):
    CAMPAIGN.run_cell(tmp_path / "bench", "lab/example", 1, 1024, 0.01, None)

  assert killed == [(4242, CAMPAIGN.signal.SIGKILL)]
  assert process.waited


def test_payload_records_runner_resource_policy_and_environment(tmp_path):
  """Result provenance names the controller and resource-limit semantics."""
  binary = tmp_path / "bench"
  source = tmp_path / "bench.cc"
  binary.write_bytes(b"binary")
  source.write_text("source", encoding="utf-8")
  environment = tmp_path / "environment.json"
  environment.write_text(
    json.dumps({
      "platform": "test",
      "memory_bytes": 16 * 1024**3,
      "operating_system": "test-os",
      "compiler": "test-cxx",
      "cmake": "test-cmake",
      "build_config": "Release",
      "source_commit": "0" * 40,
      "affinity": "test-cpu",
      "power": "external",
      "notes": "fixture",
    }),
    encoding="utf-8",
  )
  args = SimpleNamespace(
    command="capacity",
    binary=binary,
    source=source,
    environment=environment,
    timeout=10,
    memory_limit_gib=8,
    minimum_time=0.01,
    cpu=2,
  )
  payload = CAMPAIGN.base_payload(args)
  assert payload["runner_sha256"] == CAMPAIGN.sha256(
    ROOT / "tools" / "path_strategy_campaign.py"
  )
  assert payload["policy"]["cpu"] == 2
  assert payload["policy"]["resource_limit_kind"] in {
    "rlimit_as",
    "sampled_rss_watchdog_20ms",
  }


def test_all_distinct_capacity_reaches_fixture_ceiling():
  """The distinct-goal ladder reaches its exact registration boundary."""
  assert CAMPAIGN.ALL_DISTINCT_CAPACITY_COUNTS[-1] == (512 - 1) * 4


def test_capacity_ladders_extend_past_the_completed_preflight_envelope():
  """Capacity mode includes the approved next grid and request rungs."""
  assert CAMPAIGN.CAPACITY_EXTENTS == (512, 1024, 2048, 4096, 8192, 16384)
  assert CAMPAIGN.CAPACITY_COUNTS == (
    1000,
    2048,
    4096,
    8192,
    16384,
    32768,
    65536,
    131072,
  )
