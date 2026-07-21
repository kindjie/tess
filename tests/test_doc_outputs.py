"""Tests for documented example-output synchronization."""

from __future__ import annotations

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

import check_doc_outputs as cdo  # noqa: E402


MARKDOWN = """\
# Guide

<!-- tess-output: quickstart source=examples/quickstart.cc -->
```text
path cost: 14
expanded nodes: 15
```
<!-- /tess-output -->
"""


def write_binary(path: Path, *, stdout: str, status: int = 0) -> Path:
  script = f"#!/bin/sh\ncat <<'EOF'\n{stdout}\nEOF\nexit {status}\n"
  path.write_text(script, encoding="utf-8")
  path.chmod(0o755)
  return path


def write_fixture(root: Path) -> None:
  (root / "docs").mkdir()
  (root / "README.md").write_text(MARKDOWN, encoding="utf-8")


def test_extract_output_blocks_reads_source_and_body():
  blocks = cdo.extract_output_blocks(MARKDOWN, "README.md")
  assert blocks == [
    cdo.OutputBlock(
      name="quickstart",
      source="examples/quickstart.cc",
      body="path cost: 14\nexpanded nodes: 15",
      document="README.md",
    )
  ]


def test_extract_output_blocks_rejects_duplicate_names():
  with pytest.raises(ValueError, match="duplicate output block 'quickstart'"):
    cdo.extract_output_blocks(MARKDOWN + "\n" + MARKDOWN, "README.md")


def test_extract_output_blocks_rejects_malformed_markers():
  corrupted = MARKDOWN.replace(
    "<!-- /tess-output -->", "<!-- /tess-output-typo -->"
  )

  with pytest.raises(ValueError, match="malformed tess-output block"):
    cdo.extract_output_blocks(corrupted, "README.md")


def test_check_repository_reports_malformed_markers(tmp_path):
  write_fixture(tmp_path)
  readme = tmp_path / "README.md"
  readme.write_text(
    readme.read_text(encoding="utf-8").replace("```text", "``text"),
    encoding="utf-8",
  )
  binary = write_binary(
    tmp_path / "quickstart", stdout="path cost: 14\nexpanded nodes: 15"
  )

  failures = cdo.check_repository(
    tmp_path, {"examples/quickstart.cc": binary}
  )

  assert failures == [
    "README.md: malformed tess-output block; found 1 opening and 1 closing "
    "markers for 0 well-formed blocks",
    "binary mapping for examples/quickstart.cc matches no documented "
    "output block",
  ]


def test_check_repository_accepts_matching_output(tmp_path):
  write_fixture(tmp_path)
  binary = write_binary(
    tmp_path / "quickstart", stdout="path cost: 14\nexpanded nodes: 15"
  )

  failures = cdo.check_repository(
    tmp_path, {"examples/quickstart.cc": binary}
  )

  assert failures == []


def test_check_repository_reports_drifted_output(tmp_path):
  write_fixture(tmp_path)
  binary = write_binary(
    tmp_path / "quickstart", stdout="path cost: 15\nexpanded nodes: 16"
  )

  failures = cdo.check_repository(
    tmp_path, {"examples/quickstart.cc": binary}
  )

  assert failures == [
    "README.md: output block 'quickstart' differs from "
    "examples/quickstart.cc output"
  ]


def test_check_repository_reports_missing_binary_mapping(tmp_path):
  write_fixture(tmp_path)

  failures = cdo.check_repository(tmp_path, {})

  assert failures == [
    "README.md: output block 'quickstart' has no binary mapping for "
    "examples/quickstart.cc"
  ]


def test_check_repository_reports_unused_binary_mapping(tmp_path):
  write_fixture(tmp_path)
  quickstart = write_binary(
    tmp_path / "quickstart", stdout="path cost: 14\nexpanded nodes: 15"
  )
  stray = write_binary(tmp_path / "stray", stdout="unused")

  failures = cdo.check_repository(
    tmp_path,
    {"examples/quickstart.cc": quickstart, "examples/stray.cc": stray},
  )

  assert failures == [
    "binary mapping for examples/stray.cc matches no documented output block"
  ]


def test_check_repository_reports_failing_binary(tmp_path):
  write_fixture(tmp_path)
  binary = write_binary(
    tmp_path / "quickstart",
    stdout="path cost: 14\nexpanded nodes: 15",
    status=3,
  )

  failures = cdo.check_repository(
    tmp_path, {"examples/quickstart.cc": binary}
  )

  assert failures == [f"{binary.as_posix()} exited with status 3"]


def test_check_repository_reports_absent_binary(tmp_path):
  write_fixture(tmp_path)
  binary = tmp_path / "missing"

  failures = cdo.check_repository(
    tmp_path, {"examples/quickstart.cc": binary}
  )

  assert failures == [f"binary does not exist: {binary.as_posix()}"]


def test_parse_mappings_reads_source_binary_pairs():
  assert cdo.parse_mappings(["examples/quickstart.cc=build/quickstart"]) == {
    "examples/quickstart.cc": Path("build/quickstart")
  }


def test_parse_mappings_rejects_malformed_pair():
  with pytest.raises(ValueError, match="expected source=binary mapping"):
    cdo.parse_mappings(["examples/quickstart.cc"])
