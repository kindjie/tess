"""Fail-closed validator for tess.budgeted_progress.v1 artifacts.

Validates the suite-specific schema emitted by the budgeted-progress
benchmark harness (docs/planning/budgeted-progress-benchmarks.md,
section 12): unknown or malformed semantic schema versions fail
closed; the two flow conservation identities are recomputed from the
counters rather than trusted from the flags; suppressed percentiles
must be null exactly when their sample base is below the section 11.4
minimums; flow counters must be non-negative integers before any
identity arithmetic; the experiment kind must be a known v1 kind; and
mode rules hold (saturated cells omit the deadline group, classes,
and stability verdict and settle at zero; demand-limited cells carry
the deadline group, a boolean flow_stable, and a non-empty classes
array; unpaced cells carry no frame-start lag; cell artifacts carry a
null capacity band).
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

SCHEMA = "tess.budgeted_progress.v1"
SUITE_VERSION = 1

PERCENTILE_MINIMUMS = {"p50": 20, "p95": 200, "p99": 2000, "p999": 20000}

REQUIRED_BLOCKS = ("run", "experiment", "trace", "flow", "summary",
                   "calibration")

TERMINAL_BUCKETS = ("completed", "cancelled", "superseded", "stale", "failed",
                    "dropped_after_admission")

EXPERIMENT_KINDS = ("isolated_saturated", "isolated_arrival_rate",
                    "mixed_current_fidelity", "mixed_existing_quanta")

FLOW_COUNTER_KEYS = ("offered", "admitted", "rejected",
                     "coalesced_into_pending", "completed", "cancelled",
                     "superseded", "stale", "failed",
                     "dropped_after_admission", "offered_work_units",
                     "consumed_work_units", "outstanding_current",
                     "outstanding_high_water", "inventory_tick_weighted",
                     "residence_ticks_accumulated",
                     "oldest_outstanding_age_ticks")

CLASS_REQUIRED_KEYS = ("class_id", "deadline_allowance_ticks",
                       "useful_completions", "cohort_admitted",
                       "deadline_success_rate", "lateness_ticks",
                       "starved_items")


class ValidationError(Exception):
  """One artifact rule violation."""


def _require(condition: bool, message: str) -> None:
  if not condition:
    raise ValidationError(message)


def check_percentile_family(name: str, family: object) -> None:
  """Validate one percentile family against the sample minimums."""
  _require(isinstance(family, dict), f"{name}: family must be an object")
  assert isinstance(family, dict)
  _require(isinstance(family.get("sample_base"), str) and family["sample_base"],
           f"{name}: sample_base must be a non-empty string")
  samples = family.get("samples")
  _require(isinstance(samples, int) and samples >= 0,
           f"{name}: samples must be a non-negative integer")
  assert isinstance(samples, int)
  for key, minimum in PERCENTILE_MINIMUMS.items():
    value = family.get(key)
    if samples < minimum:
      _require(value is None,
               f"{name}.{key}: must be null below {minimum} samples "
               f"(saw {value!r} at {samples})")
    else:
      _require(isinstance(value, int),
               f"{name}.{key}: must publish at {samples} samples")
  max_value = family.get("max")
  if samples == 0:
    _require(max_value is None, f"{name}.max: must be null with no samples")
  else:
    _require(isinstance(max_value, int),
             f"{name}.max: must publish whenever samples exist")


def check_flow(flow: dict) -> None:
  """Recompute and enforce both flow conservation identities."""
  # Domain first: negative or non-integer counters could otherwise
  # satisfy the identity arithmetic while being meaningless.
  for key in FLOW_COUNTER_KEYS:
    value = flow.get(key)
    _require(isinstance(value, int) and not isinstance(value, bool)
             and value >= 0,
             f"flow.{key}: must be a non-negative integer (saw {value!r})")
  offered = flow["offered"]
  admitted = flow["admitted"]
  _require(
      offered == admitted + flow["rejected"] + flow["coalesced_into_pending"],
      "flow: admission identity violated")
  terminal = sum(flow[bucket] for bucket in TERMINAL_BUCKETS)
  _require(admitted == terminal + flow["outstanding_current"],
           "flow: retention identity violated")
  _require(flow.get("admission_identity_ok") is True,
           "flow: admission_identity_ok flag must be true")
  _require(flow.get("retention_identity_ok") is True,
           "flow: retention_identity_ok flag must be true")


def check_artifact(document: dict) -> None:
  """Validate one parsed artifact document, raising on violation."""
  _require(document.get("schema") == SCHEMA,
           f"unknown schema {document.get('schema')!r}: failing closed")
  _require(document.get("suite_version") == SUITE_VERSION,
           f"unknown suite_version {document.get('suite_version')!r}: "
           "failing closed")
  for block in REQUIRED_BLOCKS:
    _require(isinstance(document.get(block), dict), f"missing block {block!r}")

  experiment = document["experiment"]
  summary = document["summary"]
  pacing = experiment.get("pacing")
  _require(pacing in ("paced", "unpaced"), f"invalid pacing {pacing!r}")
  kind = experiment.get("kind")
  _require(kind in EXPERIMENT_KINDS,
           f"unknown experiment kind {kind!r}: failing closed")
  if kind == "isolated_arrival_rate":
    rate_num = experiment.get("arrival_rate_num")
    rate_den = experiment.get("arrival_rate_den")
    _require(isinstance(rate_num, int) and not isinstance(rate_num, bool)
             and rate_num > 0,
             "arrival-rate cells must carry a positive arrival_rate_num")
    _require(isinstance(rate_den, int) and not isinstance(rate_den, bool)
             and rate_den > 0,
             "arrival-rate cells must carry a positive arrival_rate_den")

  check_flow(document["flow"])

  for family_key in ("frame_elapsed_ns", "overshoot_quantum_tail_ns",
                     "overshoot_mandatory_ns"):
    _require(family_key in summary, f"summary missing {family_key}")
    check_percentile_family(f"summary.{family_key}", summary[family_key])

  if pacing == "unpaced":
    _require("frame_start_lag_ns" not in summary,
             "unpaced cells must omit frame_start_lag_ns")
  else:
    check_percentile_family("summary.frame_start_lag_ns",
                            summary["frame_start_lag_ns"])

  saturated = kind == "isolated_saturated"
  deadline_keys = ("deadline_success_rate", "lateness_ticks",
                   "oldest_age_ticks", "starved_items")
  if saturated:
    for key in deadline_keys:
      _require(key not in summary,
               f"saturated cells must omit {key} (omit, never zero)")
    _require("flow_stable" not in summary,
             "saturated cells carry no flow-stability verdict")
    _require("classes" not in document,
             "saturated cells carry no demand classes")
    _require(experiment.get("settlement_ticks") == 0,
             "saturated cells settle at zero ticks")
  else:
    for key in deadline_keys:
      _require(key in summary, f"demand-limited cells must carry {key}")
    check_percentile_family("summary.lateness_ticks",
                            summary["lateness_ticks"])
    check_percentile_family("summary.oldest_age_ticks",
                            summary["oldest_age_ticks"])
    _require(isinstance(summary.get("flow_stable"), bool),
             "demand-limited cells must carry a boolean flow_stable "
             "verdict")
    classes = document.get("classes")
    _require(isinstance(classes, list) and classes,
             "demand-limited cells must carry a non-empty classes array")
    assert isinstance(classes, list)
    for index, entry in enumerate(classes):
      _require(isinstance(entry, dict), f"classes[{index}] must be an object")
      for key in CLASS_REQUIRED_KEYS:
        _require(key in entry, f"classes[{index}] missing {key}")
      check_percentile_family(f"classes[{index}].lateness_ticks",
                              entry["lateness_ticks"])

  _require(summary.get("capacity_band") is None,
           "cell artifacts must carry a null capacity_band")

  calibration = document["calibration"]
  for family_key in ("clock_read_cost_ns", "empty_controller_loop_ns"):
    _require(family_key in calibration, f"calibration missing {family_key}")
    check_percentile_family(f"calibration.{family_key}",
                            calibration[family_key])


def validate_file(path: Path) -> list[str]:
  """Validate one artifact file, returning failure messages."""
  try:
    document = json.loads(path.read_text(encoding="utf-8"))
  except (OSError, json.JSONDecodeError) as error:
    return [f"{path}: unreadable or malformed JSON: {error}"]
  if not isinstance(document, dict):
    return [f"{path}: artifact root must be an object"]
  try:
    check_artifact(document)
  except (ValidationError, KeyError, TypeError) as error:
    return [f"{path}: {error}"]
  return []


def main(argv: list[str] | None = None) -> int:
  """Validate every artifact path given on the command line."""
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("artifacts", nargs="+", type=Path,
                      help="artifact JSON files to validate")
  arguments = parser.parse_args(argv)
  failures: list[str] = []
  for path in arguments.artifacts:
    failures.extend(validate_file(path))
  for failure in failures:
    print(f"error: {failure}", file=sys.stderr)
  return 1 if failures else 0


if __name__ == "__main__":
  sys.exit(main())
