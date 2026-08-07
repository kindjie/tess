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


AFFECTED = frozenset({"block", "core", "ops", "path"})
UNAFFECTED = frozenset({"gpu", "query"})
KNOWN = AFFECTED | UNAFFECTED


def valid_inputs():
  coverage = {}
  enabled_counterparts = {}
  tests = []
  for subsystem in sorted(AFFECTED):
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
      "schema_version": 2,
      "affected_subsystems": coverage,
      "unaffected_subsystems": {
        subsystem: f"{subsystem} needs no runtime coverage"
        for subsystem in sorted(UNAFFECTED)
      },
      "enabled_counterparts": enabled_counterparts,
    },
    {"tests": tests},
  )


def test_valid_manifest_matches_both_exception_modes():
  manifest, ctest_data = valid_inputs()
  assert checker.validate_manifest(manifest, ctest_data, KNOWN) == []


def test_validator_rejects_an_unclassified_subsystem():
  manifest, ctest_data = valid_inputs()

  failures = checker.validate_manifest(
    manifest, ctest_data, KNOWN | {"newsubsystem"}
  )

  assert (
    "newsubsystem: subsystem is unclassified; add it to "
    "affected_subsystems or unaffected_subsystems"
  ) in failures


def test_validator_rejects_a_subsystem_that_no_longer_exists():
  manifest, ctest_data = valid_inputs()

  failures = checker.validate_manifest(manifest, ctest_data, KNOWN - {"query"})

  assert "query: no such subsystem under include/tess" in failures


def test_validator_rejects_double_classification_and_empty_reasons():
  manifest, ctest_data = valid_inputs()
  manifest["unaffected_subsystems"]["core"] = "  "

  failures = checker.validate_manifest(manifest, ctest_data, KNOWN)

  assert "subsystems in both affected and unaffected: core" in failures
  assert "core: unaffected_subsystems needs a non-empty reason" in failures


def test_discover_subsystems_reads_directories_only(tmp_path):
  (tmp_path / "block").mkdir()
  (tmp_path / "gpu").mkdir()
  (tmp_path / "tess.h").write_text("", encoding="utf-8")

  assert checker.discover_subsystems(tmp_path) == frozenset({"block", "gpu"})


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

  failures = checker.validate_manifest(manifest, ctest_data, KNOWN)

  assert "unmapped exception-free runtime test: NoExceptions.Unmapped" in (
    failures
  )
  assert "path: missing enabled counterpart Enabled.path" in failures


def test_checked_in_manifest_classifies_every_real_subsystem():
  manifest = json.loads(
    (REPO_ROOT / "tests" / "no_exceptions_manifest.json").read_text(
      encoding="utf-8"
    )
  )
  known = checker.discover_subsystems(REPO_ROOT / "include" / "tess")

  assert manifest["schema_version"] == 2
  affected = set(manifest["affected_subsystems"])
  unaffected = set(manifest["unaffected_subsystems"])
  assert affected | unaffected == known
  assert not affected & unaffected
  assert set(manifest["enabled_counterparts"]) == affected
  assert all(
    reason.strip() for reason in manifest["unaffected_subsystems"].values()
  )
