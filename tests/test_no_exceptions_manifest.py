"""Tests for exception-free runtime manifest validation."""

import json
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import check_no_exceptions_manifest as checker  # noqa: E402


def entry(name, *labels):
  return {
    "name": name,
    "properties": [{"name": "LABELS", "value": [";".join(labels)]}],
  }


def valid_inputs():
  coverage = {}
  enabled_counterparts = {}
  tests = []
  for subsystem in sorted(checker.AFFECTED_SUBSYSTEMS):
    name = f"NoExceptions.{subsystem}"
    coverage[subsystem] = [name]
    enabled_counterparts[subsystem] = [f"Enabled.{subsystem}"]
    tests.append(
      entry(
        name,
        "target:tess_no_exceptions_test",
        "config:noexceptions",
        f"subsystem:{subsystem}",
      )
    )
    tests.append(
      entry(
        f"Enabled.{subsystem}",
        f"target:enabled_{subsystem}_test",
        f"subsystem:{subsystem}",
      )
    )
  return (
    {
      "schema_version": 1,
      "affected_subsystems": coverage,
      "enabled_counterparts": enabled_counterparts,
    },
    {"tests": tests},
  )


def test_valid_manifest_matches_both_exception_modes():
  manifest, ctest_data = valid_inputs()
  assert checker.validate_manifest(manifest, ctest_data) == []


def test_validator_rejects_unmapped_runtime_and_missing_enabled_coverage():
  manifest, ctest_data = valid_inputs()
  ctest_data["tests"].append(
    entry(
      "NoExceptions.Unmapped",
      "target:tess_no_exceptions_test",
      "config:noexceptions",
      "subsystem:core",
    )
  )
  ctest_data["tests"] = [
    test
    for test in ctest_data["tests"]
    if test["name"] != "Enabled.path"
  ]

  failures = checker.validate_manifest(manifest, ctest_data)

  assert "unmapped exception-free runtime test: NoExceptions.Unmapped" in (
    failures
  )
  assert "path: missing enabled counterpart Enabled.path" in failures


def test_checked_in_manifest_has_the_exact_affected_subsystems():
  manifest = json.loads(
    (REPO_ROOT / "tests" / "no_exceptions_manifest.json").read_text(
      encoding="utf-8"
    )
  )
  assert manifest["schema_version"] == 1
  assert set(manifest["affected_subsystems"]) == checker.AFFECTED_SUBSYSTEMS
  assert set(manifest["enabled_counterparts"]) == checker.AFFECTED_SUBSYSTEMS
