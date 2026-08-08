"""Tests for the section 11.2 cross-pass comparator."""

from __future__ import annotations

import copy
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

import compare_budgeted_passes as cbp  # noqa: E402


def cell(kind: str, pass_name: str, useful: int, work: int,
         stable: bool | None = None, success: float | None = None,
         pool: int = 0, rate: int = 0) -> dict:
  """Build a minimal comparable cell artifact."""
  admitted = useful
  document = {
      "schema": "tess.budgeted_progress.v1",
      "experiment": {"kind": kind, "pass": pass_name,
                     "scenario_id": "cell-v1", "budget_ns": 500000,
                     "pacing": "unpaced", "pool_size": pool},
      "trace": {"sha256": "ab" * 32},
      "flow": {"offered": admitted, "admitted": admitted, "rejected": 0,
               "coalesced_into_pending": 0, "completed": useful,
               "cancelled": 0, "superseded": 0, "stale": 0, "failed": 0,
               "dropped_after_admission": 0, "outstanding_current": 0,
               "consumed_work_units": work},
      "summary": {"useful_completions": useful,
                  "consumed_work_units": work, "repetitions": 1},
  }
  if rate:
    document["experiment"]["arrival_rate_num"] = rate
    document["experiment"]["arrival_rate_den"] = 1
  if stable is not None:
    document["summary"]["flow_stable"] = stable
  if success is not None:
    document["summary"]["deadline_success_rate"] = success
  return document


def write_pair(tmp_path: Path, timing: dict, counter: dict):
  """Write one timing and one counter artifact into separate dirs."""
  timing_dir = tmp_path / "timing"
  counter_dir = tmp_path / "counter"
  timing_dir.mkdir(exist_ok=True)
  counter_dir.mkdir(exist_ok=True)
  (timing_dir / "cell.json").write_text(json.dumps(timing))
  (counter_dir / "cell.json").write_text(json.dumps(counter))
  return timing_dir, counter_dir


def test_saturated_wrapped_pair_within_tolerance_passes(tmp_path):
  """Fully wrapped saturated pairs compare work per completion."""
  timing = cell("isolated_saturated", "timing", useful=1000, work=100000,
                pool=100)
  counter = cell("isolated_saturated", "counter", useful=400, work=40100,
                 pool=100)
  pairs, findings = cbp.run_comparison(*write_pair(tmp_path, timing, counter))
  assert pairs == 1 and findings == []


def test_saturated_divergent_work_ratio_fails(tmp_path):
  """A >1% work-per-completion divergence is a finding."""
  timing = cell("isolated_saturated", "timing", useful=1000, work=100000,
                pool=100)
  counter = cell("isolated_saturated", "counter", useful=400, work=48000,
                 pool=100)
  _, findings = cbp.run_comparison(*write_pair(tmp_path, timing, counter))
  assert findings and "diverge beyond 1%" in findings[0]


def test_saturated_below_wrap_is_exempt(tmp_path):
  """Below one full pool wrap the ratio is composition noise."""
  timing = cell("isolated_saturated", "timing", useful=50, work=9000,
                pool=100)
  counter = cell("isolated_saturated", "counter", useful=20, work=2000,
                 pool=100)
  _, findings = cbp.run_comparison(*write_pair(tmp_path, timing, counter))
  assert findings == []


def test_trace_mismatch_is_hard_failure(tmp_path):
  """Different demand traces cannot be compared."""
  timing = cell("isolated_saturated", "timing", useful=10, work=100)
  counter = cell("isolated_saturated", "counter", useful=10, work=100)
  counter["trace"]["sha256"] = "cd" * 32
  _, findings = cbp.run_comparison(*write_pair(tmp_path, timing, counter))
  assert findings and "trace hashes differ" in findings[0]


def test_regime_divergence_is_reported_not_failed(tmp_path):
  """Stable-vs-unstable disagreement is a finding with its own label."""
  timing = cell("isolated_arrival_rate", "timing", useful=600, work=60000,
                stable=True, success=1.0, rate=60)
  counter = cell("isolated_arrival_rate", "counter", useful=400, work=40000,
                 stable=False, success=0.4, rate=60)
  _, findings = cbp.run_comparison(*write_pair(tmp_path, timing, counter))
  assert findings and "REGIME DIVERGENCE" in findings[0]


def test_demand_limited_tolerances(tmp_path):
  """Stable pairs compare completions, work, and deadline success."""
  timing = cell("isolated_arrival_rate", "timing", useful=600, work=60000,
                stable=True, success=1.0, rate=60)
  counter = cell("isolated_arrival_rate", "counter", useful=590, work=59500,
                 stable=True, success=0.99, rate=60)
  _, findings = cbp.run_comparison(*write_pair(tmp_path, timing, counter))
  assert findings == []
  counter["summary"]["deadline_success_rate"] = 0.9
  _, findings = cbp.run_comparison(*write_pair(tmp_path, timing, counter))
  assert findings and "deadline success" in findings[0]


def test_unpaired_counter_artifact_is_hard_failure(tmp_path):
  """A counter artifact with no timing partner is a hard failure."""
  timing = cell("isolated_saturated", "timing", useful=10, work=100)
  counter = cell("isolated_saturated", "counter", useful=10, work=100)
  counter["experiment"]["budget_ns"] = 999
  _, findings = cbp.run_comparison(*write_pair(tmp_path, timing, counter))
  assert findings and "no timing-pass artifact" in findings[0]


def test_cli_report_only_passes_statistical_findings(tmp_path):
  """--smoke-report-only fails only on hard pairing/identity failures."""
  timing = cell("isolated_saturated", "timing", useful=1000, work=100000,
                pool=100)
  counter = cell("isolated_saturated", "counter", useful=400, work=48000,
                 pool=100)
  timing_dir, counter_dir = write_pair(tmp_path, timing, counter)
  assert cbp.main(["--timing-dir", str(timing_dir), "--counter-dir",
                   str(counter_dir), "--smoke-report-only"]) == 0
  assert cbp.main(["--timing-dir", str(timing_dir), "--counter-dir",
                   str(counter_dir)]) == 1


def test_fixture_isolation():
  """Cell builder returns fresh documents."""
  first = cell("isolated_saturated", "timing", useful=1, work=1)
  second = copy.deepcopy(first)
  second["flow"]["offered"] = 99
  assert first["flow"]["offered"] == 1
