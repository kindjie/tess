"""Tests for development-versus-release documentation checks."""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

import check_doc_versions as cdv  # noqa: E402


def write_fixture(root: Path, *, readme_package: str = "0.4") -> None:
  (root / "cmake").mkdir()
  (root / "docs").mkdir()
  (root / "cmake" / "tess-version.cmake").write_text(
    "set(TESS_VERSION 0.4.0)\n", encoding="utf-8"
  )
  (root / "CHANGELOG.md").write_text(
    "## [Unreleased]\n\n## [0.3.0] - 2026-07-17\n",
    encoding="utf-8",
  )
  (root / "README.md").write_text(
    "The latest release is `v0.3.0`. This checkout documents the "
    "unreleased `v0.4.0` development API.\n\n"
    f"find_package(tess {readme_package} CONFIG REQUIRED)\n\n"
    "GIT_TAG v0.3.0\n",
    encoding="utf-8",
  )
  (root / "docs" / "index.md").write_text(
    "This site documents the unreleased `v0.4.0` development API.\n",
    encoding="utf-8",
  )
  (root / "docs" / "packaging.md").write_text(
    "Current checkout: `find_package(tess 0.4 CONFIG REQUIRED)`.\n"
    "Latest release: `GIT_TAG v0.3.0`.\n",
    encoding="utf-8",
  )


def write_release_fixture(root: Path) -> None:
  write_fixture(root)
  (root / "CHANGELOG.md").write_text(
    "## [Unreleased]\n\n## [0.4.0] - 2026-07-20\n",
    encoding="utf-8",
  )
  (root / "README.md").write_text(
    "The latest release is `v0.4.0`. This checkout documents the "
    "`v0.4.0` release.\n\n"
    "find_package(tess 0.4 CONFIG REQUIRED)\n\n"
    "GIT_TAG v0.4.0\n",
    encoding="utf-8",
  )
  (root / "docs" / "index.md").write_text(
    "This site documents the `v0.4.0` release.\n", encoding="utf-8"
  )
  (root / "docs" / "packaging.md").write_text(
    "Release: `find_package(tess 0.4 CONFIG REQUIRED)`.\n"
    "Source: `GIT_TAG v0.4.0`.\n",
    encoding="utf-8",
  )


def test_check_repository_accepts_separate_development_and_release_versions(
  tmp_path,
):
  write_fixture(tmp_path)

  assert cdv.check_repository(tmp_path) == []


def test_check_repository_rejects_release_constraint_for_current_checkout(
  tmp_path,
):
  write_fixture(tmp_path, readme_package="0.3")

  assert cdv.check_repository(tmp_path) == [
    "README.md: current-checkout find_package must request 0.4, not 0.3"
  ]


def test_check_repository_rejects_stale_later_find_package(tmp_path):
  write_fixture(tmp_path)
  readme = tmp_path / "README.md"
  readme.write_text(
    readme.read_text(encoding="utf-8")
    + "\nfind_package(tess 0.3 CONFIG REQUIRED)\n",
    encoding="utf-8",
  )

  assert cdv.check_repository(tmp_path) == [
    "README.md: current-checkout find_package must request 0.4, not 0.3"
  ]


def test_check_repository_accepts_readme_without_find_package(tmp_path):
  write_fixture(tmp_path)
  readme = tmp_path / "README.md"
  readme.write_text(
    "\n".join(
      line
      for line in readme.read_text(encoding="utf-8").splitlines()
      if "find_package" not in line
    )
    + "\n",
    encoding="utf-8",
  )

  assert cdv.check_repository(tmp_path) == []


def test_check_repository_accepts_release_checkout(tmp_path):
  write_release_fixture(tmp_path)

  assert cdv.check_repository(tmp_path) == []


def test_check_repository_rejects_unreleased_claim_in_release_checkout(
  tmp_path,
):
  write_release_fixture(tmp_path)
  index = tmp_path / "docs" / "index.md"
  index.write_text(
    "This site documents the unreleased `v0.4.0` development API.\n",
    encoding="utf-8",
  )

  assert cdv.check_repository(tmp_path) == [
    "docs/index.md: release checkout still describes v0.4.0 as unreleased"
  ]


def test_check_repository_rejects_source_older_than_release(tmp_path):
  write_release_fixture(tmp_path)
  (tmp_path / "cmake" / "tess-version.cmake").write_text(
    "set(TESS_VERSION 0.3.0)\n", encoding="utf-8"
  )

  assert (
    "cmake/tess-version.cmake: source version must not be older than "
    "latest release v0.4.0"
  ) in cdv.check_repository(tmp_path)
