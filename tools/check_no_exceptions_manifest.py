#!/usr/bin/env python3
"""Validate exception-free runtime coverage against enabled CTest labels."""

from __future__ import annotations

import argparse
import json
import subprocess
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_INCLUDE_ROOT = REPO_ROOT / "include" / "tess"


def discover_subsystems(include_root: Path) -> frozenset[str]:
  """Every subsystem directory under `include/tess`.

  Deriving this rather than pinning a list means a newly added subsystem
  cannot silently skip exception-free classification: the manifest must
  place it under `affected_subsystems` or `unaffected_subsystems` before
  this check passes.
  """
  return frozenset(
    child.name
    for child in include_root.iterdir()
    if child.is_dir() and not child.name.startswith(".")
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


def validate_manifest(
  manifest: dict, ctest_data: dict, known_subsystems: frozenset[str]
) -> list[str]:
  failures: list[str] = []
  if manifest.get("schema_version") != 2:
    failures.append("manifest schema_version must be 2")

  coverage = manifest.get("affected_subsystems")
  if not isinstance(coverage, dict):
    return failures + ["affected_subsystems must be an object"]

  unaffected = manifest.get("unaffected_subsystems")
  if not isinstance(unaffected, dict):
    return failures + ["unaffected_subsystems must be an object"]

  # Every subsystem must be classified exactly once. A new subsystem
  # directory fails here until it is deliberately placed on one side.
  overlap = set(coverage) & set(unaffected)
  if overlap:
    failures.append(
      "subsystems in both affected and unaffected: "
      + ", ".join(sorted(overlap))
    )
  classified = set(coverage) | set(unaffected)
  for missing in sorted(known_subsystems - classified):
    failures.append(
      f"{missing}: subsystem is unclassified; add it to "
      "affected_subsystems or unaffected_subsystems"
    )
  for unknown in sorted(classified - known_subsystems):
    failures.append(f"{unknown}: no such subsystem under include/tess")

  for subsystem, reason in sorted(unaffected.items()):
    if not isinstance(reason, str) or not reason.strip():
      failures.append(
        f"{subsystem}: unaffected_subsystems needs a non-empty reason"
      )

  enabled_counterparts = manifest.get("enabled_counterparts")
  if not isinstance(enabled_counterparts, dict):
    return failures + ["enabled_counterparts must be an object"]
  if set(enabled_counterparts) != set(coverage):
    failures.append(
      "enabled_counterparts must cover exactly the affected subsystems: "
      + ", ".join(sorted(coverage))
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
    default=REPO_ROOT / "tests" / "no_exceptions_manifest.json",
  )
  parser.add_argument(
    "--include-root",
    type=Path,
    default=DEFAULT_INCLUDE_ROOT,
    help="directory whose subdirectories name the subsystems",
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
  known_subsystems = discover_subsystems(args.include_root)
  failures = validate_manifest(
    manifest, json.loads(result.stdout), known_subsystems
  )
  if failures:
    for failure in failures:
      print(f"error: {failure}")
    return 1
  affected = len(manifest["affected_subsystems"])
  print(
    f"Exception-free runtime manifest classifies {len(known_subsystems)} "
    f"subsystems: {affected} affected with runtime coverage, "
    f"{len(manifest['unaffected_subsystems'])} recorded as unaffected."
  )
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
