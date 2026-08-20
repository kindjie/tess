"""Topology tests for first-push range selection."""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

import git_hooks  # noqa: E402


def _git(repo: Path, *args: str) -> str:
  result = subprocess.run(
    ["git", "-C", str(repo), *args],
    check=True,
    capture_output=True,
    text=True,
  )
  return result.stdout.strip()


def _commit(repo: Path, name: str, content: str) -> str:
  path = repo / name
  path.parent.mkdir(parents=True, exist_ok=True)
  path.write_text(content, encoding="utf-8")
  _git(repo, "add", name)
  _git(repo, "commit", "-q", "-m", f"change {name}")
  return _git(repo, "rev-parse", "HEAD")


@pytest.fixture
def branch_repo(tmp_path: Path) -> tuple[Path, str, str]:
  repo = tmp_path / "repo"
  repo.mkdir()
  _git(repo, "init", "-q", "--initial-branch=trunk")
  _git(repo, "config", "user.name", "Test User")
  _git(repo, "config", "user.email", "test" + "@" + "example.invalid")
  _git(repo, "config", "commit.gpgSign", "false")
  base = _commit(repo, "README.md", "base\n")
  remote_url = str(tmp_path / "remote.git")
  _git(repo, "remote", "add", "upstream", remote_url)
  _git(repo, "update-ref", "refs/remotes/upstream/trunk", base)
  _git(
    repo,
    "symbolic-ref",
    "refs/remotes/upstream/HEAD",
    "refs/remotes/upstream/trunk",
  )
  _git(repo, "switch", "-q", "-c", "topic")
  return repo, remote_url, base


def _ref(local_sha: str, remote_ref: str = "refs/heads/topic"):
  return git_hooks.PushRef(
    "refs/heads/topic",
    local_sha,
    remote_ref,
    "0" * len(local_sha),
  )


def test_new_branch_uses_remote_default_merge_base(branch_repo):
  repo, remote_url, _base = branch_repo
  tip = _commit(repo, "docs/guide.md", "guide\n")

  names = git_hooks.push_range_paths(
    [_ref(tip)], ["upstream", remote_url], repo_root=repo
  )

  assert names == ["docs/guide.md"]
  assert git_hooks.classify_push_paths(names, frozenset())[0] == (
    "build-only"
  )


def test_new_branch_source_change_selects_affected_tests(branch_repo):
  repo, remote_url, _base = branch_repo
  tip = _commit(repo, "include/tess/path/new.h", "#pragma once\n")

  names = git_hooks.push_range_paths(
    [_ref(tip)], ["upstream", remote_url], repo_root=repo
  )

  assert git_hooks.classify_push_paths(names, frozenset()) == (
    "select",
    frozenset({"subsystem:path"}),
  )


def test_existing_update_does_not_require_hook_arguments(branch_repo):
  repo, _remote_url, base = branch_repo
  tip = _commit(repo, "docs/guide.md", "guide\n")
  ref = git_hooks.PushRef(
    "refs/heads/topic", tip, "refs/heads/topic", base
  )

  assert git_hooks.push_range_paths([ref], repo_root=repo) == [
    "docs/guide.md"
  ]


def test_mixed_new_and_existing_ranges_are_unioned(branch_repo):
  repo, remote_url, _base = branch_repo
  docs_tip = _commit(repo, "docs/guide.md", "guide\n")
  source_tip = _commit(repo, "include/tess/query/new.h", "#pragma once\n")
  existing = git_hooks.PushRef(
    "refs/heads/existing",
    source_tip,
    "refs/heads/existing",
    docs_tip,
  )

  names = git_hooks.push_range_paths(
    [_ref(docs_tip), existing],
    ["upstream", remote_url],
    repo_root=repo,
  )

  assert set(names) == {"docs/guide.md", "include/tess/query/new.h"}


@pytest.mark.parametrize(
  "args",
  [
    [],
    ["upstream"],
    ["upstream", ""],
    ["", "location"],
    ["upstream", "location", "extra"],
  ],
)
def test_new_branch_requires_exact_nonempty_hook_arguments(
  branch_repo, args
):
  repo, _remote_url, _base = branch_repo
  tip = _commit(repo, "docs/guide.md", "guide\n")

  assert git_hooks.push_range_paths(
    [_ref(tip)], args, repo_root=repo
  ) is None


def test_new_branch_rejects_unknown_remote_or_location(branch_repo):
  repo, remote_url, _base = branch_repo
  tip = _commit(repo, "docs/guide.md", "guide\n")

  assert git_hooks.push_range_paths(
    [_ref(tip)], ["missing", remote_url], repo_root=repo
  ) is None
  assert git_hooks.push_range_paths(
    [_ref(tip)], ["upstream", f"{remote_url}-other"], repo_root=repo
  ) is None


def test_new_branch_accepts_any_effective_push_url(branch_repo, tmp_path):
  repo, _remote_url, _base = branch_repo
  first = str(tmp_path / "push-one.git")
  second = str(tmp_path / "push-two.git")
  _git(repo, "config", "--add", "remote.upstream.pushurl", first)
  _git(repo, "config", "--add", "remote.upstream.pushurl", second)
  tip = _commit(repo, "docs/guide.md", "guide\n")

  assert git_hooks.push_range_paths(
    [_ref(tip)], ["upstream", second], repo_root=repo
  ) == ["docs/guide.md"]


def test_new_branch_rejects_cross_remote_default(branch_repo, tmp_path):
  repo, remote_url, base = branch_repo
  tip = _commit(repo, "docs/guide.md", "guide\n")
  _git(repo, "remote", "add", "other", str(tmp_path / "other.git"))
  _git(repo, "update-ref", "refs/remotes/other/trunk", base)
  _git(
    repo,
    "symbolic-ref",
    "refs/remotes/upstream/HEAD",
    "refs/remotes/other/trunk",
  )

  assert git_hooks.push_range_paths(
    [_ref(tip)], ["upstream", remote_url], repo_root=repo
  ) is None


def test_new_branch_rejects_missing_default_or_disconnected_history(
  branch_repo,
):
  repo, remote_url, _base = branch_repo
  tip = _commit(repo, "docs/guide.md", "guide\n")
  _git(repo, "symbolic-ref", "--delete", "refs/remotes/upstream/HEAD")
  assert git_hooks.push_range_paths(
    [_ref(tip)], ["upstream", remote_url], repo_root=repo
  ) is None

  _git(
    repo,
    "symbolic-ref",
    "refs/remotes/upstream/HEAD",
    "refs/remotes/upstream/trunk",
  )
  tree = _git(repo, "rev-parse", "HEAD^{tree}")
  orphan = _commit_tree(repo, tree)
  assert git_hooks.push_range_paths(
    [_ref(orphan)], ["upstream", remote_url], repo_root=repo
  ) is None


@pytest.mark.parametrize("base", [b"not-an-oid\n", b"0" * 40 + b"\n"])
def test_new_branch_rejects_malformed_merge_base(monkeypatch, base):
  def fake_git_bytes(argv, _repo_root):
    if argv[:3] == ["remote", "get-url", "--push"]:
      return b"location\n"
    if argv[0] == "check-ref-format":
      return b""
    if argv[0] == "symbolic-ref":
      return b"refs/remotes/upstream/trunk\n"
    if argv[:2] == ["merge-base", "--all"]:
      return base
    raise AssertionError(argv)

  monkeypatch.setattr(git_hooks, "git_bytes", fake_git_bytes)

  assert git_hooks.push_range_paths(
    [_ref("a" * 40)], ["upstream", "location"]
  ) is None


def _commit_tree(
  repo: Path, tree: str, *parents: str, message: str = "graph commit"
) -> str:
  command = ["git", "-C", str(repo), "commit-tree", tree]
  for parent in parents:
    command.extend(["-p", parent])
  result = subprocess.run(
    command,
    input=f"{message}\n",
    check=True,
    capture_output=True,
    text=True,
  )
  return result.stdout.strip()


def test_new_branch_rejects_multiple_merge_bases(branch_repo):
  repo, remote_url, base = branch_repo
  tree = _git(repo, "rev-parse", f"{base}^{{tree}}")
  left = _commit_tree(repo, tree, base, message="left")
  right = _commit_tree(repo, tree, base, message="right")
  default_tip = _commit_tree(repo, tree, left, right)
  branch_tip = _commit_tree(repo, tree, right, left)
  _git(repo, "update-ref", "refs/remotes/upstream/trunk", default_tip)

  assert len(
    _git(repo, "merge-base", "--all", default_tip, branch_tip).splitlines()
  ) == 2
  assert git_hooks.push_range_paths(
    [_ref(branch_tip)], ["upstream", remote_url], repo_root=repo
  ) is None


def test_new_tag_still_fails_open(branch_repo):
  repo, remote_url, _base = branch_repo
  tip = _commit(repo, "docs/guide.md", "guide\n")

  assert git_hooks.push_range_paths(
    [_ref(tip, "refs/tags/v1")],
    ["upstream", remote_url],
    repo_root=repo,
  ) is None


def test_arbitrary_new_ref_still_fails_open(branch_repo):
  repo, remote_url, _base = branch_repo
  tip = _commit(repo, "docs/guide.md", "guide\n")

  assert git_hooks.push_range_paths(
    [_ref(tip, "refs/notes/review")],
    ["upstream", remote_url],
    repo_root=repo,
  ) is None


@pytest.mark.parametrize(
  "remote_ref", ["refs/heads/", "refs/heads/../tags/v1"]
)
def test_malformed_new_branch_ref_fails_open(branch_repo, remote_ref):
  repo, remote_url, _base = branch_repo
  tip = _commit(repo, "docs/guide.md", "guide\n")

  assert git_hooks.push_range_paths(
    [_ref(tip, remote_ref)],
    ["upstream", remote_url],
    repo_root=repo,
  ) is None


def test_main_forwards_pre_push_destination(monkeypatch):
  seen: list[list[str]] = []
  monkeypatch.setattr(
    sys, "argv", ["git_hooks.py", "pre-push", "upstream", "location"]
  )
  monkeypatch.setattr(git_hooks.os, "chdir", lambda _path: None)
  monkeypatch.setattr(
    git_hooks, "pre_push", lambda args: seen.append(args) or 0
  )

  assert git_hooks.main() == 0
  assert seen == [["upstream", "location"]]


@pytest.mark.parametrize(
  ("path", "expected_ctest"),
  [
    ("docs/guide.md", []),
    (
      "include/tess/path/new.h",
      [[
        "ctest",
        "--preset",
        "dev",
        "-L",
        "(^|;)(prepush:always|subsystem:path)(;|$)",
      ]],
    ),
  ],
)
def test_pre_push_routes_destination_and_selection(
  monkeypatch, path, expected_ctest
):
  ref = _ref("a" * 40)
  observed = []
  commands = []

  def fake_paths(updates, hook_args):
    observed.append((updates, hook_args))
    return [path]

  def fake_run(argv, **_kwargs):
    commands.append(argv)
    stdout = f"{ref.local_sha}\n" if argv == [
      "git", "rev-parse", "HEAD"
    ] else ""
    return subprocess.CompletedProcess(argv, 0, stdout=stdout)

  monkeypatch.delenv("TESS_PREPUSH_FULL", raising=False)
  monkeypatch.setattr(git_hooks, "read_push_refs", lambda: [ref])
  monkeypatch.setattr(git_hooks, "push_range_paths", fake_paths)
  monkeypatch.setattr(git_hooks, "run", fake_run)

  assert git_hooks.pre_push(["upstream", "location"]) == 0
  assert observed == [([ref], ["upstream", "location"])]
  assert [command for command in commands if command[0] == "ctest"] == (
    expected_ctest
  )


def test_pre_push_delete_only_skips_all_commands(monkeypatch):
  ref = git_hooks.PushRef(
    "(delete)", "0" * 40, "refs/heads/gone", "b" * 40
  )
  monkeypatch.setattr(git_hooks, "read_push_refs", lambda: [ref])
  monkeypatch.setattr(
    git_hooks,
    "run",
    lambda *_args, **_kwargs: pytest.fail("delete-only push ran a command"),
  )

  assert git_hooks.pre_push(["upstream", "location"]) == 0
