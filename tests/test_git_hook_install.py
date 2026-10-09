"""Real Git configuration, lifetime and dispatch coverage for hook installation."""

from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

import git_hooks  # noqa: E402


@pytest.fixture(autouse=True)
def isolate_git_repository_environment(monkeypatch):
  names = subprocess.check_output(
    ["git", "rev-parse", "--local-env-vars"], text=True
  ).splitlines()
  for name in names:
    monkeypatch.delenv(name, raising=False)


def _config_hook_repo(tmp_path, monkeypatch):
  monkeypatch.setenv("GIT_CONFIG_GLOBAL", os.devnull)
  monkeypatch.setenv("GIT_CONFIG_NOSYSTEM", "1")
  repo = tmp_path / "checkout"
  repo.mkdir()
  subprocess.run(["git", "init", "-q", str(repo)], check=True)
  monkeypatch.chdir(repo)
  monkeypatch.setattr(git_hooks, "REPO_ROOT", repo)
  monkeypatch.setattr(git_hooks, "SCRIPT_PATH", repo / "tools/git_hooks.py")
  subprocess.run(
    ["git", "config", "--local", "core.hooksPath", "tools/git-hooks"],
    check=True,
  )
  return repo


def test_config_hook_install_real_git_is_idempotent_and_checkout_relative(
  tmp_path, monkeypatch
):
  repo = _config_hook_repo(tmp_path, monkeypatch)
  git_hooks.install_config_hooks()
  git_hooks.install_config_hooks()
  for name in git_hooks.HOOK_NAMES:
    events = subprocess.check_output(
      ["git", "config", "--local", "--get-all", f"hook.tess-{name}.event"],
      text=True,
    ).splitlines()
    assert events == [name]
  command = subprocess.check_output(
    ["git", "config", "--local", "--get", "hook.tess-pre-commit.command"],
    text=True,
  ).strip()
  assert str(repo) not in command
  other = tmp_path / "other-checkout"
  (other / "tools").mkdir(parents=True)
  (other / "tools/git_hooks.py").write_text(
    "from pathlib import Path\nPath('invoked').write_text('active checkout')\n"
  )
  subprocess.run(command, shell=True, cwd=other, check=True)
  assert (other / "invoked").read_text() == "active checkout"
  assert subprocess.run(
    ["git", "config", "--local", "--get", "core.hooksPath"],
    capture_output=True,
  ).returncode == 1


def test_config_hooks_reject_copied_checkout_interpreter_before_writes(
  tmp_path, monkeypatch
):
  repo = _config_hook_repo(tmp_path, monkeypatch)
  interpreter = repo / ".venv/bin/python"
  interpreter.parent.mkdir(parents=True)
  interpreter.write_text("copied interpreter placeholder")
  monkeypatch.setattr(git_hooks.sys, "executable", str(interpreter))
  with pytest.raises(ValueError, match="outside"):
    git_hooks.install_config_hooks()
  assert subprocess.check_output(
    ["git", "config", "--local", "--get", "core.hooksPath"], text=True
  ).strip() == "tools/git-hooks"
  assert subprocess.run(
    ["git", "config", "--local", "--get-regexp", r"^hook\.tess-"],
    capture_output=True,
  ).returncode == 1


def test_config_hooks_resolve_external_interpreter_symlink(tmp_path, monkeypatch):
  repo = _config_hook_repo(tmp_path, monkeypatch)
  external = Path(sys.executable).resolve()
  interpreter = repo / "python-link"
  try:
    interpreter.symlink_to(external)
  except OSError:
    pytest.skip("platform does not permit an unprivileged symlink")
  monkeypatch.setattr(git_hooks.sys, "executable", str(interpreter))
  git_hooks.install_config_hooks()
  command = subprocess.check_output(
    ["git", "config", "--local", "--get", "hook.tess-pre-commit.command"],
    text=True,
  ).strip()
  assert str(repo) not in command
  assert git_hooks.command_line([str(external)]) in command


def test_config_hooks_dispatch_all_events_from_checkout(tmp_path, monkeypatch):
  repo = _config_hook_repo(tmp_path, monkeypatch)
  # Modern Git returns 1 for an empty hook list as well as unsupported Git.
  # Seed a harmless event so capability probing cannot silently skip coverage.
  subprocess.run(
    ["git", "config", "--local", "hook.fixture-probe.event", "pre-commit"],
    check=True,
  )
  subprocess.run(
    ["git", "config", "--local", "hook.fixture-probe.command",
     git_hooks.command_line([sys.executable, "-c", "pass"])],
    check=True,
  )
  if not git_hooks.supports_config_hooks():
    pytest.skip("installed Git does not support config-defined hooks")
  subprocess.run(
    ["git", "config", "--local", "--remove-section", "hook.fixture-probe"],
    check=True,
  )
  (repo / "tools").mkdir()
  (repo / "tools/git_hooks.py").write_text(
    "from pathlib import Path\nimport sys\n"
    "Path('invoked-' + sys.argv[1]).write_text(str(Path.cwd()))\n"
  )
  git_hooks.install_config_hooks()
  for name in git_hooks.HOOK_NAMES:
    subprocess.run(["git", "hook", "run", name, "--", "fixture"], check=True)
    assert (repo / f"invoked-{name}").read_text() == str(repo)


def test_failed_config_hook_install_preserves_existing_enforcement(
  tmp_path, monkeypatch
):
  _config_hook_repo(tmp_path, monkeypatch)
  for name in git_hooks.HOOK_NAMES:
    subprocess.run(
      ["git", "config", "--local", f"hook.tess-{name}.event", name],
      check=True,
    )
  original_run = git_hooks.run

  def fail_install(argv, **kwargs):
    if "hook.tess-commit-msg.command" in argv and "--unset-all" not in argv:
      raise subprocess.CalledProcessError(1, argv)
    return original_run(argv, **kwargs)

  monkeypatch.setattr(git_hooks, "run", fail_install)
  with pytest.raises(subprocess.CalledProcessError):
    git_hooks.install_config_hooks()
  assert subprocess.check_output(
    ["git", "config", "--local", "--get", "core.hooksPath"], text=True
  ).strip() == "tools/git-hooks"
  for name in git_hooks.HOOK_NAMES:
    assert subprocess.check_output(
      ["git", "config", "--local", "--get", f"hook.tess-{name}.event"],
      text=True,
    ).strip() == name


def test_failed_config_hook_install_rolls_back_partial_registration(
  tmp_path, monkeypatch
):
  # A failure after earlier events were registered must not leave those
  # configured hooks active beside core.hooksPath, which would run each
  # check twice; the configuration returns to its prior state instead.
  _config_hook_repo(tmp_path, monkeypatch)
  original_run = git_hooks.run

  def fail_install(argv, **kwargs):
    if "hook.tess-commit-msg.command" in argv and "--unset-all" not in argv:
      raise subprocess.CalledProcessError(1, argv)
    return original_run(argv, **kwargs)

  monkeypatch.setattr(git_hooks, "run", fail_install)
  with pytest.raises(subprocess.CalledProcessError):
    git_hooks.install_config_hooks()
  assert subprocess.check_output(
    ["git", "config", "--local", "--get", "core.hooksPath"], text=True
  ).strip() == "tools/git-hooks"
  assert subprocess.run(
    ["git", "config", "--local", "--get-regexp", r"^hook\.tess-"],
    capture_output=True,
  ).returncode == 1
