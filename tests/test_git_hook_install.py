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


@pytest.fixture(autouse=True)
def isolate_interpreter_prefix(tmp_path, monkeypatch):
  # The installer reads pyvenv.cfg from sys.prefix; keep a test runner's own
  # venv (as in CI) out of every fixture unless a test supplies one.
  prefix = tmp_path / "interpreter-prefix"
  prefix.mkdir()
  monkeypatch.setattr(git_hooks.sys, "prefix", str(prefix))


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
  # No external base interpreter to fall back to.
  monkeypatch.setattr(
    git_hooks.sys, "_base_executable", str(interpreter), raising=False
  )
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
  if not git_hooks.supports_config_hooks():
    pytest.skip("installed Git does not support config-defined hooks")
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


def test_config_hooks_keep_stable_external_interpreter_path(
  tmp_path, monkeypatch
):
  # Package managers expose stable launchers that symlink into versioned
  # directories; resolving them pins a path that a patch upgrade removes.
  repo = _config_hook_repo(tmp_path, monkeypatch)
  versioned = tmp_path / "cellar/python/3.0.1/bin/python3"
  versioned.parent.mkdir(parents=True)
  versioned.write_text("versioned interpreter placeholder")
  stable = tmp_path / "opt/python/bin/python3"
  stable.parent.mkdir(parents=True)
  try:
    stable.symlink_to(versioned)
  except OSError:
    pytest.skip("platform does not permit an unprivileged symlink")
  monkeypatch.setattr(git_hooks.sys, "executable", str(stable))
  git_hooks.install_config_hooks()
  command = subprocess.check_output(
    ["git", "config", "--local", "--get", "hook.tess-pre-commit.command"],
    text=True,
  ).strip()
  assert git_hooks.command_line([str(stable)]) in command
  assert "cellar" not in command
  assert str(repo) not in command


def test_install_rejects_unsupported_python_before_config_writes(
  tmp_path, monkeypatch
):
  _config_hook_repo(tmp_path, monkeypatch)
  monkeypatch.setattr(git_hooks.sys, "version_info", (3, 9, 18))
  assert git_hooks.install_hooks() != 0
  assert subprocess.check_output(
    ["git", "config", "--local", "--get", "core.hooksPath"], text=True
  ).strip() == "tools/git-hooks"
  assert subprocess.run(
    ["git", "config", "--local", "--get-regexp", r"^hook\.tess-"],
    capture_output=True,
  ).returncode == 1


def test_config_hooks_reject_external_link_into_checkout(tmp_path, monkeypatch):
  repo = _config_hook_repo(tmp_path, monkeypatch)
  interpreter = repo / ".venv/bin/python"
  interpreter.parent.mkdir(parents=True)
  interpreter.write_text("checkout interpreter placeholder")
  link = tmp_path / "bin/python3"
  link.parent.mkdir(parents=True)
  try:
    link.symlink_to(interpreter)
  except OSError:
    pytest.skip("platform does not permit an unprivileged symlink")
  monkeypatch.setattr(git_hooks.sys, "executable", str(link))
  with pytest.raises(ValueError, match="outside"):
    git_hooks.install_config_hooks()
  assert subprocess.run(
    ["git", "config", "--local", "--get-regexp", r"^hook\.tess-"],
    capture_output=True,
  ).returncode == 1


def _git_version() -> tuple[int, ...]:
  text = subprocess.check_output(["git", "--version"], text=True)
  return tuple(int(part) for part in text.split()[2].split(".")[:2])


def test_config_hook_probe_succeeds_before_any_hook_exists(
  tmp_path, monkeypatch
):
  # An empty `git hook list` exits 1 even where config hooks are supported,
  # so a fresh clone must not fall back to the compatibility hooks path.
  _config_hook_repo(tmp_path, monkeypatch)
  subprocess.run(
    ["git", "config", "--local", "--unset", "core.hooksPath"], check=True
  )
  assert git_hooks.supports_config_hooks() == (_git_version() >= (2, 54))


def _stable_launcher(tmp_path: Path) -> Path:
  versioned = tmp_path / "cellar/python/3.0.1/bin/python3"
  versioned.parent.mkdir(parents=True)
  versioned.write_text("versioned interpreter placeholder")
  stable = tmp_path / "opt/python/bin/python3"
  stable.parent.mkdir(parents=True)
  stable.symlink_to(versioned)
  return stable


def test_config_hooks_follow_checkout_venv_only_to_its_stable_base(
  tmp_path, monkeypatch
):
  # A venv inside the checkout links to the base interpreter as invoked;
  # follow links only while inside the checkout, then stop.
  repo = _config_hook_repo(tmp_path, monkeypatch)
  try:
    stable = _stable_launcher(tmp_path)
    venv = repo / ".venv/bin"
    venv.mkdir(parents=True)
    (venv / "python").symlink_to(stable)
    (venv / "python3").symlink_to("python")
  except OSError:
    pytest.skip("platform does not permit an unprivileged symlink")
  monkeypatch.setattr(git_hooks.sys, "executable", str(venv / "python3"))
  git_hooks.install_config_hooks()
  command = subprocess.check_output(
    ["git", "config", "--local", "--get", "hook.tess-pre-commit.command"],
    text=True,
  ).strip()
  assert git_hooks.command_line([str(stable)]) in command
  assert "cellar" not in command


def test_config_hooks_treat_other_worktrees_as_checkout(tmp_path, monkeypatch):
  # Hook configuration is shared by every worktree, so an interpreter inside
  # any of them may disappear with it.
  repo = _config_hook_repo(tmp_path, monkeypatch)
  subprocess.run(
    ["git", "-c", "user.name=t", "-c", "user.email=t" + "@" + "example.invalid",
     "commit", "-q", "--allow-empty", "-m", "base"],
    check=True,
  )
  other = tmp_path / "other-worktree"
  subprocess.run(
    ["git", "worktree", "add", "-q", "--detach", str(other)], check=True
  )
  interpreter = other / ".venv/bin/python3"
  interpreter.parent.mkdir(parents=True)
  interpreter.write_text("copied interpreter placeholder")
  monkeypatch.setattr(git_hooks.sys, "executable", str(interpreter))
  monkeypatch.setattr(
    git_hooks.sys, "_base_executable", str(interpreter), raising=False
  )
  with pytest.raises(ValueError, match="outside"):
    git_hooks.install_config_hooks()
  assert subprocess.run(
    ["git", "config", "--local", "--get-regexp", r"^hook\.tess-"],
    capture_output=True,
  ).returncode == 1


def _recorded_command() -> str:
  return subprocess.check_output(
    ["git", "config", "--local", "--get", "hook.tess-pre-commit.command"],
    text=True,
  ).strip()


def test_config_hooks_follow_venv_reached_through_symlinked_parent(
  tmp_path, monkeypatch
):
  # Worktree roots are real paths; a venv reached through a symlinked parent
  # such as /tmp or /var must still count as inside the checkout.
  repo = _config_hook_repo(tmp_path, monkeypatch)
  try:
    stable = _stable_launcher(tmp_path)
    venv = repo / ".venv/bin"
    venv.mkdir(parents=True)
    (venv / "python3").symlink_to(stable)
    alias = tmp_path / "alias"
    alias.symlink_to(repo)
  except OSError:
    pytest.skip("platform does not permit an unprivileged symlink")
  monkeypatch.setattr(
    git_hooks.sys, "executable", str(alias / ".venv/bin/python3")
  )
  git_hooks.install_config_hooks()
  assert git_hooks.command_line([str(stable)]) in _recorded_command()


def test_config_hooks_follow_relative_links_as_the_filesystem_does(
  tmp_path, monkeypatch
):
  repo = _config_hook_repo(tmp_path, monkeypatch)
  try:
    stable = _stable_launcher(tmp_path)
    venv = repo / ".venv/bin"
    venv.mkdir(parents=True)
    (venv / "python3").symlink_to(stable)
    (repo / "a/b/c").mkdir(parents=True)
    (repo / "sub").symlink_to("a/b/c")
    (repo / "sub/py").symlink_to("../../../.venv/bin/python3")
  except OSError:
    pytest.skip("platform does not permit an unprivileged symlink")
  monkeypatch.setattr(git_hooks.sys, "executable", str(repo / "sub/py"))
  git_hooks.install_config_hooks()
  assert git_hooks.command_line([str(stable)]) in _recorded_command()


def test_config_hooks_use_base_interpreter_of_copied_checkout_venv(
  tmp_path, monkeypatch
):
  # Windows venvs and --copies venvs hold a copied interpreter; record the
  # external base interpreter the venv was created from instead.
  repo = _config_hook_repo(tmp_path, monkeypatch)
  interpreter = repo / ".venv/bin/python"
  interpreter.parent.mkdir(parents=True)
  interpreter.write_text("copied interpreter placeholder")
  base = tmp_path / "opt/python/bin/python3"
  base.parent.mkdir(parents=True)
  base.write_text("base interpreter placeholder")
  monkeypatch.setattr(git_hooks.sys, "executable", str(interpreter))
  monkeypatch.setattr(
    git_hooks.sys, "_base_executable", str(base), raising=False
  )
  git_hooks.install_config_hooks()
  assert git_hooks.command_line([str(base)]) in _recorded_command()


def test_copied_venv_prefers_stable_home_from_pyvenv_cfg(tmp_path, monkeypatch):
  # pyvenv.cfg records the stable launcher directory as `home`, while the
  # base executable may already be resolved into a versioned directory.
  repo = _config_hook_repo(tmp_path, monkeypatch)
  venv = repo / ".venv"
  interpreter = venv / "bin/python3"
  interpreter.parent.mkdir(parents=True)
  interpreter.write_text("copied interpreter placeholder")
  home = tmp_path / "opt/python/bin"
  home.mkdir(parents=True)
  minor = f"python{sys.version_info[0]}.{sys.version_info[1]}"
  (home / minor).write_text("stable launcher placeholder")
  (venv / "pyvenv.cfg").write_text(f"home = {home}\nversion = 3.0.1\n")
  monkeypatch.setattr(git_hooks.sys, "executable", str(interpreter))
  monkeypatch.setattr(git_hooks.sys, "prefix", str(venv))
  monkeypatch.setattr(
    git_hooks.sys,
    "_base_executable",
    str(tmp_path / "cellar/python/3.0.1/bin" / minor),
    raising=False,
  )
  git_hooks.install_config_hooks()
  assert git_hooks.command_line([str(home / minor)]) in _recorded_command()


def test_config_hooks_normalize_relative_link_leaving_the_checkout(
  tmp_path, monkeypatch
):
  # A relative last hop must not record ".." segments through a worktree
  # that may later be removed.
  repo = _config_hook_repo(tmp_path, monkeypatch)
  external = tmp_path / "ext/bin/python3"
  external.parent.mkdir(parents=True)
  external.write_text("external interpreter placeholder")
  venv = repo / ".venv/bin"
  venv.mkdir(parents=True)
  try:
    (venv / "python3").symlink_to("../../../ext/bin/python3")
  except OSError:
    pytest.skip("platform does not permit an unprivileged symlink")
  monkeypatch.setattr(git_hooks.sys, "executable", str(venv / "python3"))
  git_hooks.install_config_hooks()
  command = _recorded_command()
  assert ".." not in command
  expected = external.parent.resolve() / "python3"
  assert git_hooks.command_line([str(expected)]) in command
