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

  tdd = tdd_path.read_text(encoding="utf-8")
  required_contracts = (
    "source compatibility",
    "binary ABI compatibility",
    "SameMajorVersion",
    "experimental/maintenance.h",
    "WorkerPoolPhaseExecutor",
    "_HAS_EXCEPTIONS=0",
    "GCC runtime",
    "coverage-guided fuzzing",
    "v1.0.0-rc.1",
  )
  for contract in required_contracts:
    assert contract in tdd
