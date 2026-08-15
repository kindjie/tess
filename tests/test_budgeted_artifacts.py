"""Tests for the budgeted-progress artifact validator (fail-closed)."""

from __future__ import annotations

import copy
import json
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

import check_budgeted_artifacts as cba  # noqa: E402


def family(samples: int, value: int = 1, base: str = "all_measured_frames"):
  """Build one percentile family dict at the given sample count."""
  minimums = cba.PERCENTILE_MINIMUMS
  out = {"sample_base": base, "samples": samples}
  for key, minimum in minimums.items():
    out[key] = value if samples >= minimum else None
  out["max"] = value if samples > 0 else None
  return out


def saturated_artifact() -> dict:
  """Build a valid saturated-mode cell artifact."""
  return {
      "schema": cba.SCHEMA,
      "suite_version": cba.SUITE_VERSION,
      "run": {"commit": "c", "machine_fingerprint": "m", "compiler": "cc",
              "bench_flags": "-O2"},
      "experiment": {
          "kind": "isolated_saturated",
          "scenario_id": "astar-unit-roomcorridor-512",
          "workload_refs": ["path/astar_unit"],
          "seed": 6029575,
          "frame_hz_num": 60,
          "frame_hz_den": 1,
          "sim_tps": 60,
          "sim_speed": "1x",
          "max_ticks_per_frame": 8,
          "pacing": "unpaced",
          "budget_scope": "frame",
          "budget_ns": 500000,
          "settlement_ticks": 0,
          "executor": {"kind": "serial", "workers": 1},
      },
      "trace": {"version": 1, "sha256": "ab" * 32},
      "flow": {
          "offered": 10, "admitted": 9, "rejected": 1,
          "coalesced_into_pending": 0, "completed": 9, "cancelled": 0,
          "superseded": 0, "stale": 0, "failed": 0,
          "dropped_after_admission": 0, "offered_work_units": 9,
          "consumed_work_units": 9, "outstanding_current": 0,
          "outstanding_high_water": 1, "inventory_tick_weighted": 0,
          "residence_ticks_accumulated": 0,
          "oldest_outstanding_age_ticks": 0,
          "admission_identity_ok": True, "retention_identity_ok": True,
      },
      "summary": {
          "measured_frames": 600,
          "repetitions": 10,
          "useful_completions": 9,
          "consumed_work_units": 9,
          "overshoot_frame_rate": 0.01,
          "frame_elapsed_ns": family(6000),
          "overshoot_quantum_tail_ns": family(6000),
          "overshoot_mandatory_ns": family(6000),
          "capacity_band": None,
          "peak_rss_bytes": 1024,
          "correctness_hash": None,
      },
      "calibration": {
          "clock_identity": "steady_clock",
          "clock_read_cost_ns": family(1000),
          "empty_controller_loop_ns": family(1000),
      },
  }


def demand_limited_artifact() -> dict:
  """Build a valid paced demand-limited cell artifact."""
  artifact = saturated_artifact()
  artifact["experiment"]["kind"] = "isolated_arrival_rate"
  artifact["experiment"]["pacing"] = "paced"
  artifact["experiment"]["settlement_ticks"] = 64
  artifact["experiment"]["arrival_rate_num"] = 600
  artifact["experiment"]["arrival_rate_den"] = 1
  artifact["summary"].update({
      "frame_start_lag_ns": family(6000, base="paced_frames"),
      "measured_wall_ns": 15_000_000,
      "useful_per_wall_second": 600.0,
      "deadline_success_rate": 0.995,
      "lateness_ticks": family(0, base="completed_cohort_items"),
      "oldest_age_ticks": family(6000, base="per_tick_observations"),
      "starved_items": 0,
      "flow_stable": True,
  })
  artifact["classes"] = [{
      "class_id": "interactive_path",
      "deadline_allowance_ticks": 1,
      "useful_completions": 9,
      "cohort_admitted": 9,
      "deadline_success_rate": 0.995,
      "lateness_ticks": family(0, base="completed_cohort_items"),
      "starved_items": 0,
  }]
  return artifact


def write(tmp_path: Path, document: dict) -> Path:
  """Write the artifact document to a temp file and return its path."""
  path = tmp_path / "artifact.json"
  path.write_text(json.dumps(document), encoding="utf-8")
  return path


def test_valid_saturated_artifact_passes(tmp_path):
  """A well-formed saturated artifact validates cleanly."""
  assert cba.validate_file(write(tmp_path, saturated_artifact())) == []


def test_valid_demand_limited_artifact_passes(tmp_path):
  """A well-formed demand-limited artifact validates cleanly."""
  assert cba.validate_file(write(tmp_path, demand_limited_artifact())) == []


@pytest.mark.parametrize("schema", ["tess.budgeted_progress.v2", None, 7])
def test_unknown_schema_fails_closed(tmp_path, schema):
  """Unknown or missing schema identifiers fail closed."""
  document = saturated_artifact()
  document["schema"] = schema
  failures = cba.validate_file(write(tmp_path, document))
  assert failures and "failing closed" in failures[0]


def test_unknown_suite_version_fails_closed(tmp_path):
  """An unknown suite_version fails closed."""
  document = saturated_artifact()
  document["suite_version"] = 2
  failures = cba.validate_file(write(tmp_path, document))
  assert failures and "failing closed" in failures[0]


def test_malformed_json_fails_closed(tmp_path):
  """Unparseable JSON fails closed."""
  path = tmp_path / "artifact.json"
  path.write_text("{not json", encoding="utf-8")
  assert cba.validate_file(path)


def test_admission_identity_recomputed_not_trusted(tmp_path):
  """The admission identity is recomputed, not trusted from flags."""
  document = saturated_artifact()
  document["flow"]["offered"] = 11  # Flag still claims OK.
  failures = cba.validate_file(write(tmp_path, document))
  assert failures and "admission identity" in failures[0]


def test_retention_identity_recomputed_not_trusted(tmp_path):
  """The retention identity is recomputed, not trusted from flags."""
  document = saturated_artifact()
  document["flow"]["completed"] = 8
  failures = cba.validate_file(write(tmp_path, document))
  assert failures and "retention identity" in failures[0]


def test_undersampled_percentile_must_be_null(tmp_path):
  """Percentiles below their sample minimum must be null."""
  document = saturated_artifact()
  document["summary"]["frame_elapsed_ns"]["samples"] = 100
  # p95/p99/p999 now claim values below their minimums.
  failures = cba.validate_file(write(tmp_path, document))
  assert failures and "must be null" in failures[0]


def test_sufficient_percentile_must_publish(tmp_path):
  """Percentiles at or above their minimum must publish."""
  document = saturated_artifact()
  document["summary"]["frame_elapsed_ns"]["p50"] = None
  failures = cba.validate_file(write(tmp_path, document))
  assert failures and "must publish" in failures[0]


def test_saturated_must_omit_deadline_group(tmp_path):
  """Saturated cells omit deadline metrics rather than zeroing them."""
  document = saturated_artifact()
  document["summary"]["deadline_success_rate"] = 0.0
  failures = cba.validate_file(write(tmp_path, document))
  assert failures and "omit" in failures[0]


def test_saturated_settles_at_zero(tmp_path):
  """Saturated cells record settlement_ticks of zero."""
  document = saturated_artifact()
  document["experiment"]["settlement_ticks"] = 64
  failures = cba.validate_file(write(tmp_path, document))
  assert failures and "settle at zero" in failures[0]


def test_unpaced_must_omit_frame_start_lag(tmp_path):
  """Unpaced cells carry no frame-start-lag family."""
  document = saturated_artifact()
  document["summary"]["frame_start_lag_ns"] = family(6000)
  failures = cba.validate_file(write(tmp_path, document))
  assert failures and "frame_start_lag_ns" in failures[0]


def test_demand_limited_requires_deadline_group(tmp_path):
  """Demand-limited cells must carry the full deadline group."""
  document = demand_limited_artifact()
  del document["summary"]["lateness_ticks"]
  failures = cba.validate_file(write(tmp_path, document))
  assert failures and "must carry" in failures[0]


def test_cell_capacity_band_must_be_null(tmp_path):
  """Individual cell artifacts carry a null capacity band."""
  document = saturated_artifact()
  document["summary"]["capacity_band"] = {"confirmed_stable": 5}
  failures = cba.validate_file(write(tmp_path, document))
  assert failures and "capacity_band" in failures[0]


def test_missing_calibration_family_fails(tmp_path):
  """Missing calibration families are an error."""
  document = saturated_artifact()
  del document["calibration"]["clock_read_cost_ns"]
  failures = cba.validate_file(write(tmp_path, document))
  assert failures and "calibration" in failures[0]


def test_cli_reports_failures(tmp_path, capsys):
  """The CLI exits nonzero and prints each failure."""
  document = saturated_artifact()
  document["schema"] = "bogus"
  path = write(tmp_path, document)
  assert cba.main([str(path)]) == 1
  assert "failing closed" in capsys.readouterr().err


def test_cli_accepts_valid(tmp_path):
  """The CLI exits zero for valid artifacts."""
  path = write(tmp_path, saturated_artifact())
  assert cba.main([str(path)]) == 0


def test_unknown_experiment_kind_fails_closed(tmp_path):
  """An unknown or missing experiment kind fails closed."""
  document = saturated_artifact()
  document["experiment"]["kind"] = "isolated_saturated_v2"
  failures = cba.validate_file(write(tmp_path, document))
  assert failures and "unknown experiment kind" in failures[0]


def test_negative_flow_counter_rejected(tmp_path):
  """Negative counters are rejected before identity arithmetic."""
  document = saturated_artifact()
  document["flow"]["offered"] = -1
  document["flow"]["admitted"] = -1
  document["flow"]["completed"] = -1
  document["flow"]["rejected"] = 0
  failures = cba.validate_file(write(tmp_path, document))
  assert failures and "non-negative" in failures[0]


def test_demand_limited_requires_flow_stable(tmp_path):
  """Demand-limited cells must carry a boolean stability verdict."""
  document = demand_limited_artifact()
  del document["summary"]["flow_stable"]
  failures = cba.validate_file(write(tmp_path, document))
  assert failures and "flow_stable" in failures[0]


def test_demand_limited_requires_classes(tmp_path):
  """Demand-limited cells must carry a non-empty classes array."""
  document = demand_limited_artifact()
  del document["classes"]
  failures = cba.validate_file(write(tmp_path, document))
  assert failures and "classes" in failures[0]


def test_class_entry_requires_fields(tmp_path):
  """Each class entry must carry every required field."""
  document = demand_limited_artifact()
  del document["classes"][0]["starved_items"]
  failures = cba.validate_file(write(tmp_path, document))
  assert failures and "classes[0] missing starved_items" in failures[0]


def test_saturated_must_omit_classes(tmp_path):
  """Saturated cells carry no demand classes."""
  document = saturated_artifact()
  document["classes"] = []
  failures = cba.validate_file(write(tmp_path, document))
  assert failures and "no demand classes" in failures[0]


def test_arrival_rate_cell_requires_rate(tmp_path):
  """Arrival-rate cells must carry a positive rational rate."""
  document = demand_limited_artifact()
  del document["experiment"]["arrival_rate_num"]
  failures = cba.validate_file(write(tmp_path, document))
  assert failures and "arrival_rate_num" in failures[0]


def test_paced_requires_wall_fields(tmp_path):
  """Paced cells must carry the measured wall span and rate."""
  document = demand_limited_artifact()
  del document["summary"]["measured_wall_ns"]
  failures = cba.validate_file(write(tmp_path, document))
  assert failures and "measured_wall_ns" in failures[0]


def test_unpaced_must_omit_wall_rate(tmp_path):
  """Measured wall rates come from paced cells only."""
  document = saturated_artifact()
  document["summary"]["useful_per_wall_second"] = 100.0
  failures = cba.validate_file(write(tmp_path, document))
  assert failures and "paced cells only" in failures[0]


def test_wall_rate_must_match_source_fields(tmp_path):
  """The published wall rate must be derivable from its sources."""
  document = demand_limited_artifact()
  document["summary"]["useful_per_wall_second"] = 0.0
  failures = cba.validate_file(write(tmp_path, document))
  assert failures and "does not match" in failures[0]


def test_wall_rate_must_be_finite(tmp_path):
  """Infinity and NaN fail closed."""
  document = demand_limited_artifact()
  document["summary"]["useful_per_wall_second"] = float("inf")
  failures = cba.validate_file(write(tmp_path, document))
  assert failures and "finite" in failures[0]


def test_boolean_arrival_rate_rejected(tmp_path):
  """Booleans are not integers for the rate fields (fail closed)."""
  document = demand_limited_artifact()
  document["experiment"]["arrival_rate_num"] = True
  failures = cba.validate_file(write(tmp_path, document))
  assert failures and "arrival_rate_num" in failures[0]


def point(rate: int, stable: bool, confirmation: bool = False) -> dict:
  """Build one tested search point with three repetition records."""
  rep = {"stable": stable, "useful_completions": 100, "cohort_admitted": 100,
         "cohort_deadline_met": 100 if stable else 3,
         "outstanding_growth": 0 if stable else 97,
         "oldest_age_end_ticks": 0 if stable else 40}
  return {"rate": rate, "confirmation": confirmation, "stable": stable,
          "reps": [dict(rep) for _ in range(3)]}


def search_artifact() -> dict:
  """Build a valid capacity-search summary document."""
  return {
      "schema": cba.SEARCH_SCHEMA,
      "suite_version": cba.SUITE_VERSION,
      "run": {"commit": "c", "machine_fingerprint": "m", "compiler": "cc",
              "bench_flags": ""},
      "search": {
          "scenario_id": "astar-arrival-roomcorridor-512-v1",
          "workload_refs": ["path/astar_unit"],
          "budget_ns": 500000,
          "sim_tps": 60,
          "pacing": "unpaced",
          "seed_rate": 60,
          "resolution_percent": 2,
      },
      "points": [
          point(60, stable=True),
          point(240, stable=False),
          point(120, stable=True),
          point(120, stable=True, confirmation=True),
      ],
      "capacity_band": {"confirmed_stable": 120, "lowest_unstable": 240},
      "flapping": 0,
  }


def test_valid_search_artifact_passes(tmp_path):
  """A well-formed search summary validates cleanly."""
  assert cba.validate_file(write(tmp_path, search_artifact())) == []


def test_search_band_inversion_rejected(tmp_path):
  """The band cannot invert: lowest_unstable must exceed confirmed."""
  document = search_artifact()
  # Verdict-consistent edges that nevertheless invert.
  document["points"].append(point(240, stable=True, confirmation=True))
  document["points"].append(point(120, stable=False))
  document["capacity_band"] = {"confirmed_stable": 240,
                               "lowest_unstable": 120}
  failures = cba.validate_file(write(tmp_path, document))
  assert failures and "inverted" in failures[0]


def test_search_band_must_be_tested_point(tmp_path):
  """Band edges must reference points that were actually tested."""
  document = search_artifact()
  document["capacity_band"]["confirmed_stable"] = 119
  failures = cba.validate_file(write(tmp_path, document))
  assert failures and "tested point" in failures[0]


def test_search_requires_points(tmp_path):
  """Search summaries must retain every tested point."""
  document = search_artifact()
  document["points"] = []
  failures = cba.validate_file(write(tmp_path, document))
  assert failures and "tested point" in failures[0]


def test_search_band_edges_must_match_verdicts(tmp_path):
  """Membership is not enough: edges must match point verdicts."""
  document = search_artifact()
  # 120 confirmed stable; naming it lowest_unstable must fail.
  document["capacity_band"] = {"confirmed_stable": None,
                               "lowest_unstable": 120}
  failures = cba.validate_file(write(tmp_path, document))
  assert failures and "tested unstable" in failures[0]


def test_search_points_require_repetitions(tmp_path):
  """Every tested point must retain its repetition evidence."""
  document = search_artifact()
  document["points"][0]["reps"] = []
  failures = cba.validate_file(write(tmp_path, document))
  assert failures and "every repetition" in failures[0]


def test_search_requires_run_provenance(tmp_path):
  """Search summaries must carry non-empty run provenance."""
  document = search_artifact()
  document["run"]["compiler"] = ""
  failures = cba.validate_file(write(tmp_path, document))
  assert failures and "run.compiler" in failures[0]


def test_counters_only_in_counter_pass(tmp_path):
  """Detailed counters belong to counter-pass artifacts only."""
  document = saturated_artifact()
  document["counters"] = {"path": {"heap_pops": 1}}
  failures = cba.validate_file(write(tmp_path, document))
  assert failures and "counter-pass" in failures[0]
  document["experiment"]["pass"] = "counter"
  assert cba.validate_file(write(tmp_path, document)) == []


def test_unknown_pass_fails_closed(tmp_path):
  """An unknown pass marker fails closed."""
  document = saturated_artifact()
  document["experiment"]["pass"] = "pmu"
  failures = cba.validate_file(write(tmp_path, document))
  assert failures and "unknown pass" in failures[0]


def test_deep_copy_fixture_isolated():
  """Fixture builders return fresh documents per call."""
  # Guard: fixtures are rebuilt per test, not shared mutable state.
  first = saturated_artifact()
  second = copy.deepcopy(first)
  second["flow"]["offered"] = 99
  assert first["flow"]["offered"] == 10

def test_unknown_movement_tier_fails_closed(tmp_path):
  """An unrecognized tier value must fail, not default."""
  artifact = saturated_artifact()
  artifact["experiment"]["movement_tier"] = "teleport"
  errors = cba.validate_file(write(tmp_path, artifact))
  assert any("movement_tier" in error for error in errors)


def test_movement_tier_must_agree_with_scenario(tmp_path):
  """A pibt scenario with a baseline tier field is a contradiction."""
  artifact = demand_limited_artifact()
  artifact["experiment"]["kind"] = "mixed_current_fidelity"
  artifact["experiment"]["population"] = 100
  artifact["experiment"]["scenario_id"] = "colony-roomcorridor-pingpong-pibt-v1"
  artifact["experiment"]["movement_tier"] = "baseline"
  errors = cba.validate_file(write(tmp_path, artifact))
  assert any("contradicts scenario" in error for error in errors)
  # Legacy artifacts omit the field entirely and mean baseline; a
  # baseline pingpong scenario without the field stays valid.
  artifact["experiment"]["scenario_id"] = "colony-roomcorridor-pingpong-v1"
  del artifact["experiment"]["movement_tier"]
  assert cba.validate_file(write(tmp_path, artifact)) == []


def test_mixed_cells_require_realized_churn_hash(tmp_path):
  """A tier-era mixed cell without the realized hash fails closed."""
  artifact = demand_limited_artifact()
  artifact["experiment"]["kind"] = "mixed_current_fidelity"
  artifact["experiment"]["population"] = 100
  artifact["experiment"]["movement_tier"] = "baseline"
  errors = cba.validate_file(write(tmp_path, artifact))
  assert any("must record trace.realized_churn_sha256" in error
             for error in errors)
  artifact["trace"]["realized_churn_sha256"] = "ab" * 32
  assert cba.validate_file(write(tmp_path, artifact)) == []
  # Legacy mixed artifacts predate both fields and stay valid.
  del artifact["experiment"]["movement_tier"]
  del artifact["trace"]["realized_churn_sha256"]
  assert cba.validate_file(write(tmp_path, artifact)) == []


def test_realized_churn_hash_shape_is_validated(tmp_path):
  """A malformed realized-churn hash fails closed."""
  artifact = saturated_artifact()
  artifact["trace"]["realized_churn_sha256"] = "not-a-hash"
  errors = cba.validate_file(write(tmp_path, artifact))
  assert any("realized_churn_sha256" in error for error in errors)
  artifact["trace"]["realized_churn_sha256"] = "ab" * 32
  assert cba.validate_file(write(tmp_path, artifact)) == []
