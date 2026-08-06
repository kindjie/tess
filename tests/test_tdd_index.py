"""Tests for release-planning TDD coverage."""

from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


def test_v1_stabilization_tdd_records_the_amended_release_plan():
  tdd_path = REPO_ROOT / "docs" / "tdd" / "v1-stabilization.md"
  index = (REPO_ROOT / "docs" / "tdd" / "README.md").read_text(
    encoding="utf-8"
  )

  assert tdd_path.is_file()
  assert "[1.0 stabilization](v1-stabilization.md)" in index
  assert "Historical `v1` scope labels in TDDs written before v0.1" in index

  tdd = " ".join(tdd_path.read_text(encoding="utf-8").split())
  required_decisions = (
    "Stable binary ABI compatibility across tess versions, compilers, "
    "standard libraries, build modes, or mixed-version translation units "
    "is not promised.",
    "Removal waits for the next major version unless the exceptional "
    "correction process applies.",
    "The compatibility umbrella must stop including "
    "`experimental/maintenance.h`.",
    "`WorkerPoolPhaseExecutor` is promoted as stable production machinery.",
    "uses `SameMajorVersion`",
    "execute the GCC runtime suite",
    "relies on `_HAS_EXCEPTIONS=0`",
    "coverage-guided fuzzing target",
    "Recalibrate the five fields bootstrap ceilings",
    "v1.0.0-rc.1",
    "No known breaking change is introduced between the final candidate "
    "and 1.0",
  )
  for decision in required_decisions:
    assert decision in tdd
