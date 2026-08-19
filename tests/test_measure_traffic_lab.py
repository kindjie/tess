"""Unit coverage for the Traffic Lab advisory measurement runner."""

from __future__ import annotations

import importlib.util
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
  "measure_traffic_lab", ROOT / "tools" / "measure_traffic_lab.py"
)
assert SPEC is not None and SPEC.loader is not None
MEASURE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MEASURE)


def test_percentiles_are_suppressed_until_their_sample_floor():
  """p95 and p99 stay absent when their sample floors are unmet."""
  values = [float(value) for value in range(1, 200)]
  summary = MEASURE.summarize(values)

  assert summary["samples"] == 199
  assert summary["p50"] == 100.0
  assert summary["p95"] is None
  assert summary["p99"] is None


def test_percentiles_use_nearest_rank_at_supported_sample_counts():
  """Supported families use the documented nearest-rank convention."""
  values = [float(value) for value in range(1, 2001)]
  summary = MEASURE.summarize(values)

  assert summary == {
    "samples": 2000,
    "minimum": 1.0,
    "p25": 500.0,
    "p50": 1000.0,
    "p75": 1500.0,
    "p95": 1900.0,
    "p99": 1980.0,
    "maximum": 2000.0,
  }


def test_sample_parser_rejects_a_wrong_tick_count():
  """A truncated native stream cannot become a plausible artifact."""
  rows = [
    "scenario,tick,update_us,planning_us,planning_queries,waits,blocked,"
    "arrived,pending,advanced",
    "aligned,0,10.0,4.0,8,0,0,0,1016,1",
  ]

  try:
    MEASURE.parse_samples("\n".join(rows), "aligned", expected_ticks=2)
  except ValueError as error:
    assert "expected 2 samples" in str(error)
  else:
    raise AssertionError("short sample stream was accepted")


def test_timing_and_counter_passes_cannot_be_mixed():
  """Instrumented timing and uninstrumented counters fail closed."""
  plain = [{"passability_checks": 0}]
  instrumented = [{"passability_checks": 12}]

  MEASURE.validate_pass(plain, counter_pass=False)
  MEASURE.validate_pass(instrumented, counter_pass=True)
  for samples, counter_pass in ((plain, True), (instrumented, False)):
    try:
      MEASURE.validate_pass(samples, counter_pass=counter_pass)
    except ValueError as error:
      assert "counter pass" in str(error) or "timing pass" in str(error)
    else:
      raise AssertionError("incompatible measurement pass was accepted")
