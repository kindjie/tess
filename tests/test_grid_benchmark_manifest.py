"""Regression tests for the gated external grid-benchmark manifest."""

from __future__ import annotations

import json
import re
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
MANIFEST = REPO_ROOT / "tools" / "grid_benchmark_manifest.json"
PIN = "fe6351b0700a0f4e75d0bd79ce3bf5478bc60c94"


def load_manifest() -> dict:
  return json.loads(MANIFEST.read_text(encoding="utf-8"))


def test_manifest_pins_schema_source_and_revision():
  manifest = load_manifest()

  assert manifest["schema_version"] == 1
  assert manifest["upstream"]["repository"] == (
      "https://bitbucket.org/shortestpathlab/benchmarks"
  )
  assert manifest["upstream"]["revision"] == PIN
  assert manifest["database_license"] == "ODC-By-1.0"


def test_rights_gate_blocks_downloadable_entries():
  manifest = load_manifest()

  assert manifest["content_rights"]["status"] in {"blocked", "cleared"}
  if manifest["content_rights"]["status"] != "cleared":
    assert manifest["entries"] == []


def test_entries_have_safe_paths_and_sha256_when_gate_clears():
  manifest = load_manifest()

  for entry in manifest["entries"]:
    path = Path(entry["path"])
    assert not path.is_absolute()
    assert ".." not in path.parts
    assert re.fullmatch(r"[0-9a-f]{64}", entry["sha256"])
    assert entry["content_rights_basis"].strip()
