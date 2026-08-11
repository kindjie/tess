"""Tests for immutable 1.x compatibility snapshot validation."""

from __future__ import annotations

import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

import check_compatibility_snapshots as snapshots  # noqa: E402


def make_repo(root: Path) -> tuple[Path, dict[str, object]]:
  headers = {
      "stable": [
          "include/tess/pathfinding.h",
          "include/tess/simulation.h",
          "include/tess/tess.h",
      ],
      "optional-stable": ["include/tess/optional.h"],
      "experimental": [],
      "implementation-only": [],
  }
  for header in headers["stable"] + headers["optional-stable"]:
    path = root / header
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("#pragma once\nstruct StableSymbol {};\n", encoding="utf-8")
  aggregate = root / "include/tess/tess.h"
  aggregate.write_text(
      "#pragma once\n#include <tess/pathfinding.h>\nstruct StableSymbol {};\n",
      encoding="utf-8",
  )
  header_path = root / "cmake/tess-headers.json"
  header_path.parent.mkdir(parents=True)
  header_path.write_text(json.dumps(headers), encoding="utf-8")
  payload: dict[str, object] = {
      "version": "1.0.0-rc.1",
      "headers": {
          "stable": headers["stable"],
          "optional-stable": headers["optional-stable"],
      },
      "aggregate_membership": snapshots.aggregate_membership(root),
      "public_symbols": ["StableSymbol"],
      "consumer": "consumer/main.cc",
      "archive_consumer": "archives/load.cc",
      "archives": [
          {
              "path": "archives/one.bin",
              "format": 1,
              "producer_version": "1.0.0-rc.1",
              "schema": "fixed-test-schema-v1",
          }
      ],
  }
  return header_path, payload


def write_snapshot(root: Path, payload: dict[str, object]) -> Path:
  directory = root / "compatibility/1.0.0-rc.1"
  (directory / "consumer").mkdir(parents=True)
  (directory / "archives").mkdir()
  (directory / "consumer/main.cc").write_text("int main() {}\n")
  (directory / "archives/load.cc").write_text("int main() {}\n")
  (directory / "archives/one.bin").write_bytes(b"fixture")
  (directory / "manifest.json").write_text(json.dumps(payload))
  return root / "compatibility"


def test_valid_snapshot_is_a_subset_of_current_sources(tmp_path):
  header_path, payload = make_repo(tmp_path)
  snapshot_root = write_snapshot(tmp_path, payload)

  assert snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.0.0-rc.1"
  ) == []


def test_removed_contract_and_fixture_fail_deterministically(tmp_path):
  header_path, payload = make_repo(tmp_path)
  payload["headers"]["stable"].append("include/tess/removed.h")
  payload["aggregate_membership"]["include/tess/tess.h"].append(
      "include/tess/removed.h"
  )
  payload["public_symbols"].append("RemovedSymbol")
  snapshot_root = write_snapshot(tmp_path, payload)
  (snapshot_root / "1.0.0-rc.1/archives/one.bin").unlink()

  failures = snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.1.1"
  )

  assert failures == [
      "1.0.0-rc.1: stable header removed or reclassified: "
      "include/tess/removed.h",
      "1.0.0-rc.1: aggregate member removed from include/tess/tess.h: "
      "include/tess/removed.h",
      "1.0.0-rc.1: public symbol removed: RemovedSymbol",
      "1.0.0-rc.1: invalid or missing archive fixture metadata",
  ]


def test_rc1_requires_its_exact_snapshot(tmp_path):
  header_path, _ = make_repo(tmp_path)

  assert snapshots.check_snapshots(
      tmp_path, tmp_path / "compatibility", header_path, "1.0.0-rc.1"
  ) == ["release 1.0.0-rc.1: compatibility snapshot is missing"]


def test_archive_fixture_requires_fixed_schema_metadata(tmp_path):
  header_path, payload = make_repo(tmp_path)
  payload["archives"][0]["schema"] = ""
  snapshot_root = write_snapshot(tmp_path, payload)

  assert snapshots.check_snapshots(
      tmp_path, snapshot_root, header_path, "1.0.0-rc.1"
  ) == ["1.0.0-rc.1: invalid or missing archive fixture metadata"]
