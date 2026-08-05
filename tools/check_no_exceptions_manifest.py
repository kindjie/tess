#!/usr/bin/env python3
"""Validate exception-free runtime coverage against enabled CTest labels."""

from __future__ import annotations

import argparse
import json
import subprocess
from pathlib import Path


AFFECTED_SUBSYSTEMS = frozenset(
  {
    "block",
    "core",
    "experimental",
    "ops",
    "path",
    "sim",
    "storage",
    "topology",
  }
)


def test_labels(test: dict) -> set[str]:
  labels: set[str] = set()
  for prop in test.get("properties", []):
    if prop.get("name") != "LABELS":
      continue
    values = prop.get("value", [])
    if isinstance(values, str):
      values = [values]
    for value in values:
      labels.update(part for part in value.split(";") if part)
  return labels


def validate_manifest(manifest: dict, ctest_data: dict) -> list[str]:
  failures: list[str] = []
  if manifest.get("schema_version") != 1:
    failures.append("manifest schema_version must be 1")

  coverage = manifest.get("affected_subsystems")
  if not isinstance(coverage, dict):
    return failures + ["affected_subsystems must be an object"]
  if set(coverage) != AFFECTED_SUBSYSTEMS:
    failures.append(
      "affected_subsystems must be exactly: "
      + ", ".join(sorted(AFFECTED_SUBSYSTEMS))
    )

  enabled_counterparts = manifest.get("enabled_counterparts")
  if not isinstance(enabled_counterparts, dict):
    return failures + ["enabled_counterparts must be an object"]
  if set(enabled_counterparts) != AFFECTED_SUBSYSTEMS:
    failures.append(
      "enabled_counterparts must be exactly: "
      + ", ".join(sorted(AFFECTED_SUBSYSTEMS))
    )

  tests = {
    test.get("name"): test
    for test in ctest_data.get("tests", [])
    if isinstance(test.get("name"), str)
  }
  mapped: set[str] = set()
  for subsystem, names in coverage.items():
    if not isinstance(names, list) or not names:
      failures.append(f"{subsystem}: expected a non-empty test list")
      continue
    for name in names:
      if not isinstance(name, str):
        failures.append(f"{subsystem}: test names must be strings")
        continue
      mapped.add(name)
      test = tests.get(name)
      if test is None:
        failures.append(f"{subsystem}: missing CTest case {name}")
        continue
      labels = test_labels(test)
      required = {"config:noexceptions", f"subsystem:{subsystem}"}
      if not required <= labels:
        failures.append(
          f"{subsystem}: {name} lacks labels "
          + ", ".join(sorted(required - labels))
        )

  runtime_tests = {
    name
    for name, test in tests.items()
    if "target:tess_no_exceptions_test" in test_labels(test)
  }
  for name in sorted(runtime_tests - mapped):
    failures.append(f"unmapped exception-free runtime test: {name}")
  for name in sorted(mapped - runtime_tests):
    failures.append(f"manifest entry is not a runtime test: {name}")

  for subsystem, names in enabled_counterparts.items():
    if not isinstance(names, list) or not names:
      failures.append(f"{subsystem}: expected enabled counterpart tests")
      continue
    for name in names:
      if not isinstance(name, str):
        failures.append(
          f"{subsystem}: enabled counterpart names must be strings"
        )
        continue
      test = tests.get(name)
      if test is None:
        failures.append(f"{subsystem}: missing enabled counterpart {name}")
        continue
      labels = test_labels(test)
      if "config:noexceptions" in labels:
        failures.append(f"{subsystem}: enabled counterpart is noexceptions")
      label = f"subsystem:{subsystem}"
      if label not in labels:
        failures.append(f"{subsystem}: {name} lacks label {label}")
  return failures


def main() -> int:
  parser = argparse.ArgumentParser()
  parser.add_argument("--ctest-dir", required=True, type=Path)
  parser.add_argument("--config")
  parser.add_argument(
    "--manifest",
    type=Path,
    default=(
      Path(__file__).resolve().parents[1]
      / "tests"
      / "no_exceptions_manifest.json"
    ),
  )
  args = parser.parse_args()

  manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
  command = [
    "ctest",
    "--test-dir",
    str(args.ctest_dir),
    "--show-only=json-v1",
  ]
  if args.config:
    command.extend(("--build-config", args.config))
  result = subprocess.run(
    command,
    check=True,
    capture_output=True,
    text=True,
  )
  failures = validate_manifest(manifest, json.loads(result.stdout))
  if failures:
    for failure in failures:
      print(f"error: {failure}")
    return 1
  print(
    "Exception-free runtime manifest covers "
    f"{len(AFFECTED_SUBSYSTEMS)} affected subsystems."
  )
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
