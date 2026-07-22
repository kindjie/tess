"""Tests for the fail-closed CI change classifier."""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

import ci_changes  # noqa: E402


SHA_A = "a" * 40
SHA_B = "b" * 40


@pytest.mark.parametrize(
  "path",
  (
    "README.md",
    "tests/AGENTS.md",
    ".github/PULL_REQUEST_TEMPLATE.md",
    "docs/architecture/surface.json",
    "docs/doxygen-awesome/tess-theme.css",
    "mkdocs.yml",
  ),
)
def test_documentation_paths_are_recognized(path):
  assert ci_changes.is_documentation_path(path)


@pytest.mark.parametrize(
  "path",
  (
    ".github/workflows/ci.yml",
    "CMakeLists.txt",
    "include/tess/tess.h",
    "tests/fixture.txt",
    "tools/check_docs_links.py",
  ),
)
def test_code_affecting_paths_are_rejected(path):
  assert not ci_changes.is_documentation_path(path)


def test_empty_change_set_requires_full_ci():
  classification = ci_changes.classify_paths(())

  assert classification.code_required
  assert classification.reason == "no changed paths found"


def test_all_documentation_changes_use_fast_path():
  classification = ci_changes.classify_paths(
    ("README.md", "docs/guide.md", "mkdocs.yml")
  )

  assert not classification.code_required
  assert classification.reason == "documentation-only change"


def test_one_code_path_requires_full_ci():
  classification = ci_changes.classify_paths(
    ("docs/guide.md", "include/tess/tess.h")
  )

  assert classification.code_required
  assert classification.reason == "code-affecting path: 'include/tess/tess.h'"


def test_git_diff_is_nul_safe_and_disables_rename_detection():
  captured = []

  def run(command, **kwargs):
    captured.append((command, kwargs))
    return subprocess.CompletedProcess(
      command,
      0,
      stdout=b"docs/line\nbreak.md\0README.md\0",
    )

  paths = ci_changes.changed_paths(SHA_A, SHA_B, run=run)

  assert paths == ("docs/line\nbreak.md", "README.md")
  assert captured == [
    (
      (
        "git",
        "diff",
        "--name-only",
        "--no-renames",
        "-z",
        SHA_A,
        SHA_B,
        "--",
      ),
      {"check": True, "stdout": subprocess.PIPE},
    )
  ]


@pytest.mark.parametrize(
  "revision",
  ("", "0" * 40, "not-a-sha", "a" * 39, "a" * 41),
)
def test_invalid_revision_fails_closed(revision):
  classification = ci_changes.classify_range(revision, SHA_B)

  assert classification.code_required
  assert classification.reason == "invalid comparison revision"


def test_git_diff_failure_fails_closed():
  def fail(_command, **_kwargs):
    raise subprocess.CalledProcessError(128, "git diff")

  classification = ci_changes.classify_range(SHA_A, SHA_B, run=fail)

  assert classification.code_required
  assert classification.reason == "unable to inspect changed paths"


def test_cli_writes_github_output_and_reason(monkeypatch, capsys):
  monkeypatch.setattr(
    ci_changes,
    "classify_range",
    lambda _base, _head: ci_changes.Classification(
      code_required=False,
      reason="documentation-only change",
    ),
  )

  assert ci_changes.main((SHA_A, SHA_B)) == 0
  captured = capsys.readouterr()
  assert captured.out == "code_required=false\n"
  assert captured.err == "CI change classification: documentation-only change\n"
