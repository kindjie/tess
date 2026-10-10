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


def test_inventory_check_uses_the_venv_python_on_every_platform(monkeypatch):
  # Windows venvs provide python.exe but no python3.exe.
  _inventory_staged(monkeypatch)
  requested = []

  def fake_venv_tool(name):
    requested.append(name)
    return "/venv/python" if name == "python" else None

  monkeypatch.setattr(git_hooks, "venv_tool", fake_venv_tool)
  commands = []
  monkeypatch.setattr(
    git_hooks,
    "run",
    lambda argv, **kwargs: commands.append(argv)
    or subprocess.CompletedProcess(argv, 0, stdout="", stderr=""),
  )
  assert git_hooks.check_inventory_counts() == 0
  assert commands[0][0] == "/venv/python"


def test_inventory_failure_does_not_presume_a_count_mismatch(
  monkeypatch, capsys
):
  # A .venv without pytest or an unbuildable uv environment fails the
  # command too; report the output rather than asserting a mismatch.
  _inventory_staged(monkeypatch)
  monkeypatch.setattr(git_hooks, "venv_tool", lambda name: "/venv/python")
  monkeypatch.setattr(
    git_hooks,
    "run",
    lambda argv, **kwargs: subprocess.CompletedProcess(
      argv, 1, stdout="/venv/python: No module named pytest\n"
    ),
  )
  assert git_hooks.check_inventory_counts() == 1
  error = capsys.readouterr().err
  assert "No module named pytest" in error
  assert "do not match" not in error
