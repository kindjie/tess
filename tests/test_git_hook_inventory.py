"""Interpreter and dependency selection for the staged inventory tests."""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

import git_hooks  # noqa: E402


def _inventory_staged(monkeypatch):
  monkeypatch.setattr(
    git_hooks, "staged_files", lambda: ["tests/CMakeLists.txt"]
  )


def test_inventory_check_uses_locked_dev_environment_without_venv(
  monkeypatch
):
  # The hook interpreter need not carry pytest; the hash-locked uv
  # environment supplies it, as it does for the token check.
  _inventory_staged(monkeypatch)
  monkeypatch.setattr(git_hooks, "venv_tool", lambda name: None)
  monkeypatch.setattr(git_hooks.shutil, "which", lambda name: "/bin/uv-fake")
  commands = []

  def fake_run(argv, **kwargs):
    commands.append(argv)
    return subprocess.CompletedProcess(argv, 0, stdout="", stderr="")

  monkeypatch.setattr(git_hooks, "run", fake_run)
  assert git_hooks.check_inventory_counts() == 0
  assert commands == [
    git_hooks.uv_dev_command(
      "/bin/uv-fake", "python", "-m", "pytest", "-q",
      *git_hooks.INVENTORY_TESTS,
    )
  ]


def test_inventory_check_reports_missing_pytest_distinctly(
  monkeypatch, capsys
):
  _inventory_staged(monkeypatch)
  monkeypatch.setattr(git_hooks, "venv_tool", lambda name: None)
  monkeypatch.setattr(git_hooks.shutil, "which", lambda name: None)
  monkeypatch.setattr(git_hooks, "module_available", lambda name: False)

  def unexpected_run(argv, **kwargs):
    raise AssertionError(f"unexpected command {argv}")

  monkeypatch.setattr(git_hooks, "run", unexpected_run)
  assert git_hooks.check_inventory_counts() == 1
  error = capsys.readouterr().err
  assert "pytest" in error
  assert "do not match" not in error
