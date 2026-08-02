"""Tests for the long-retention benchmark data-branch layout."""

from __future__ import annotations

import json
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

import publish_benchmark_data as pub  # noqa: E402

COMMIT = "a" * 40
OTHER = "b" * 40
STAMP = "2026-08-01T12:00:00Z"


def write_baseline(directory: Path, name: str, benchmarks: int = 2) -> Path:
  """Write a plausible Google Benchmark baseline file."""
  path = directory / name
  path.write_text(
    json.dumps({
      "context": {"host_name": "runner"},
      "benchmarks": [
        {"name": f"bench_{i}", "cpu_time": 100.0 + i} for i in range(benchmarks)
      ],
    }),
    encoding="utf-8",
  )
  return path


def test_publishing_nothing_is_an_error(tmp_path: Path) -> None:
  """An empty publish must fail rather than report a healthy run.

  This is the failure the tool exists to prevent: the step succeeds,
  the history quietly stops growing, and nobody notices until a
  bisection needs data that was never stored.
  """
  with pytest.raises(pub.ToolError, match="no baseline files"):
    pub.plan_publication([], COMMIT, STAMP, "1")


def test_a_short_or_dirty_commit_is_rejected(tmp_path: Path) -> None:
  """Only a full lowercase sha keys a run, so history stays joinable."""
  source = write_baseline(tmp_path, "a.json")
  for bad in ("abc123", COMMIT.upper(), f"{COMMIT}\n", ""):
    with pytest.raises(pub.ToolError, match="full lowercase sha"):
      pub.plan_publication([source], bad, STAMP, "1")


def test_a_malformed_timestamp_is_rejected(tmp_path: Path) -> None:
  """The shard path is derived from the timestamp, so it must parse."""
  source = write_baseline(tmp_path, "a.json")
  for bad in ("2026-08-01", "not-a-time", "2026-08-01 12:00:00"):
    with pytest.raises(pub.ToolError, match="ISO-8601"):
      pub.plan_publication([source], COMMIT, bad, "1")


def test_malformed_baseline_json_names_the_file(tmp_path: Path) -> None:
  """A truncated artifact must name itself, not raise from the middle."""
  bad = tmp_path / "bad.json"
  bad.write_text('{"benchmarks": [', encoding="utf-8")
  with pytest.raises(pub.ToolError, match="bad.json: malformed JSON"):
    pub.plan_publication([bad], COMMIT, STAMP, "1")


def test_destinations_shard_by_month_and_key_by_commit(
  tmp_path: Path,
) -> None:
  """Layout keeps listings usable and makes a re-run overwrite."""
  source = write_baseline(tmp_path, "core.json")
  plan = pub.plan_publication([source], COMMIT, STAMP, "7")
  assert list(plan.files) == [f"baselines/2026/08/{COMMIT}/core.json"]
  assert plan.index_entry["benchmark_count"] == 2
  assert plan.index_entry["run_id"] == "7"


def test_writing_twice_for_one_commit_does_not_duplicate_the_index(
  tmp_path: Path,
) -> None:
  """A re-run corrects its row instead of leaving two that disagree."""
  out = tmp_path / "branch"
  source = write_baseline(tmp_path, "core.json", benchmarks=2)
  pub.write_publication(pub.plan_publication([source], COMMIT, STAMP, "1"), out)
  updated = write_baseline(tmp_path, "core.json", benchmarks=5)
  pub.write_publication(
    pub.plan_publication([updated], COMMIT, STAMP, "2"), out
  )

  index = json.loads((out / "index.json").read_text(encoding="utf-8"))
  assert len(index["runs"]) == 1
  assert index["runs"][0]["run_id"] == "2"
  assert index["runs"][0]["benchmark_count"] == 5


def test_the_index_keeps_runs_newest_first(tmp_path: Path) -> None:
  """Bisection reads backwards from now, so order is part of the format."""
  out = tmp_path / "branch"
  source = write_baseline(tmp_path, "core.json")
  pub.write_publication(
    pub.plan_publication([source], COMMIT, "2026-08-01T12:00:00Z", "1"), out
  )
  pub.write_publication(
    pub.plan_publication([source], OTHER, "2026-09-01T12:00:00Z", "2"), out
  )

  index = json.loads((out / "index.json").read_text(encoding="utf-8"))
  assert [run["commit"] for run in index["runs"]] == [OTHER, COMMIT]


def test_a_corrupt_existing_index_fails_loudly(tmp_path: Path) -> None:
  """Better to fail the publish than to silently discard the history."""
  out = tmp_path / "branch"
  out.mkdir()
  (out / "index.json").write_text("{not json", encoding="utf-8")
  source = write_baseline(tmp_path, "core.json")
  with pytest.raises(pub.ToolError, match="index is malformed"):
    pub.write_publication(
      pub.plan_publication([source], COMMIT, STAMP, "1"), out
    )


def test_the_cli_reports_failure_with_a_nonzero_status(
  tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
  """The workflow gates on the exit status, so it must be honest."""
  assert (
    pub.main([
      "--out-dir",
      str(tmp_path),
      "--commit",
      COMMIT,
      "--timestamp",
      STAMP,
    ])
    == 1
  )
  assert "no baseline files" in capsys.readouterr().err

  source = write_baseline(tmp_path, "core.json")
  assert (
    pub.main([
      "--out-dir",
      str(tmp_path / "branch"),
      "--commit",
      COMMIT,
      "--timestamp",
      STAMP,
      str(source),
    ])
    == 0
  )
