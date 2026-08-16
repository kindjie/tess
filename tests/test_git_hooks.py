"""Unit tests for tools/git_hooks.py check helpers."""

from __future__ import annotations

import base64
import os
import re
import socket
import json
import subprocess
import sys
import time
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

import git_hooks  # noqa: E402
import wait_for_browser_state  # noqa: E402


def reader_for(files: dict[str, bytes]):
  return lambda path: files.get(path, b"")


# Positive/near-miss fixtures for each PRIVATE_PATTERNS entry, in order.
# Positives are built from concatenated fragments so this test file never
# contains the flagged byte sequences itself.
PRIVATE_CASES = (
  (b"Link" + b"edIn.com/in/example", b"link" + b"edin.org/in/example"),
  (b"/Us" + b"ers/example/notes", b"/Us" + b"er/example/notes"),
  (b"/pri" + b"vate/tmp/thing", b"/pri" + b"vatex/tmp/thing"),
  (b"/ro" + b"ot/private.txt", b"/ro" + b"ots/private.txt"),
  (b"/Vol" + b"umes/Private/data", b"/Vol" + b"ume/data"),
  (b"/ho" + b"me/example/file", b"/ho" + b"me//file"),
  (b"C:" + b"\\Users\\example\\notes", b"C:" + b"relative\\notes"),
  (b"\\" + b"\\server\\share\\notes", b"\\" + b"server\\notes"),
  (b"-----BEGIN " + b"PRIV" + b"ATE KEY-----", b"BEGIN PUBLIC KEY"),
  (
    b"AW" + b"S_SECRET_ACCESS_KEY = value",
    b"AW" + b"S_REGION = value",
  ),
  (b"AK" + b"IA" + b"A" * 16, b"AK" + b"IA" + b"A" * 15),
  (b"GITHUB_TO" + b"KEN = value", b"GITHUB_TO" + b"KENS value"),
  (b"github_" + b"pat_" + b"abc123", b"github_" + b"pat_ = value"),
  (b"gh" + b"o_" + b"a" * 40, b"gh" + b"i_" + b"a" * 40),
  (b"sk-" + b"a" * 20, b"sk-" + b"a" * 19),
  (b"sk-" + b"proj-" + b"a" * 20, b"sk-" + b"proj-" + b"a" * 19),
  (b"gl" + b"pat-" + b"a" * 20, b"gl" + b"pat-" + b"a" * 19),
  (b"xox" + b"b-" + b"a" * 20, b"xox" + b"b-" + b"a" * 19),
  (b"sk_" + b"live_" + b"a" * 20, b"sk_" + b"live_" + b"a" * 19),
  (b"person" + b"@example.com", b"person at example.com"),
  (b"+1 604" + b" 555 0123", b"60455 50123"),
  (b"pass" + b"word: value", b"pass" + b"words: value"),
)


def test_private_case_table_covers_every_pattern():
  assert len(PRIVATE_CASES) == len(git_hooks.PRIVATE_PATTERNS)


def test_unc_pattern_ignores_shell_quote_escaping():
  unc_pattern = git_hooks.PRIVATE_PATTERNS[7]
  shell_escape = b"sed s/'/'" + b"\\" * 4 + b"''/g"

  assert not unc_pattern.search(shell_escape)


@pytest.mark.parametrize(
  ("index", "positive", "near_miss"),
  [(i, pos, neg) for i, (pos, neg) in enumerate(PRIVATE_CASES)],
)
def test_private_pattern_fires_and_near_miss_passes(index, positive, near_miss):
  pattern = git_hooks.PRIVATE_PATTERNS[index]
  assert pattern.search(positive), f"pattern {index} missed its fixture"
  assert not pattern.search(near_miss), f"pattern {index} matched its near-miss"


def test_load_private_patterns_adds_local_case_insensitive_regexes(tmp_path):
  path = tmp_path / "patterns"
  path.write_bytes(b"# local identities\nprivate[-_ ]consumer\n\n")

  patterns = git_hooks.load_private_patterns(path)

  assert patterns[: len(git_hooks.PRIVATE_PATTERNS)] == (
    git_hooks.PRIVATE_PATTERNS
  )
  assert patterns[-1].search(b"PRIVATE Consumer")


def test_load_private_patterns_rejects_invalid_regex(tmp_path):
  path = tmp_path / "patterns"
  path.write_bytes(b"valid\n(unclosed\n")

  with pytest.raises(ValueError, match=r"patterns:2: invalid byte regex"):
    git_hooks.load_private_patterns(path)


def test_repository_private_patterns_use_common_git_dir(tmp_path):
  def git_output(command, **kwargs):
    assert command == ["git", "rev-parse", "--git-common-dir"]
    assert kwargs["cwd"] == git_hooks.REPO_ROOT
    return f"{tmp_path}\n"

  path = git_hooks.repository_common_git_path(
    "tess-private-patterns", git_output=git_output
  )

  assert path == tmp_path / "tess-private-patterns"


def test_repository_private_patterns_resolve_relative_common_dir():
  def git_output(command, **kwargs):
    assert command == ["git", "rev-parse", "--git-common-dir"]
    assert kwargs["cwd"] == git_hooks.REPO_ROOT
    return ".git\n"

  path = git_hooks.repository_common_git_path(
    "tess-private-patterns", git_output=git_output
  )

  assert path == git_hooks.REPO_ROOT / ".git" / "tess-private-patterns"


def test_ensure_identity_patterns_adds_escaped_full_name(tmp_path):
  path = tmp_path / "patterns"

  added = git_hooks.ensure_identity_patterns(path, "Example O'Person")

  assert added == 1
  patterns = git_hooks.load_private_patterns(path)
  assert patterns[-1].search(b"EXAMPLE O'PERSON")
  assert not patterns[-1].search(b"example project")
  assert not patterns[-1].search(b"another person")
  assert git_hooks.ensure_identity_patterns(path, "Example O'Person") == 0


def test_ensure_identity_patterns_does_not_ban_bot_name_tokens(tmp_path):
  path = tmp_path / "patterns"

  assert git_hooks.ensure_identity_patterns(path, "CI Bot") == 1
  pattern = git_hooks.load_private_patterns(path)[-1]
  assert not pattern.search(b"CI runs the bot checks")
  assert not pattern.search(b"this GitHub action builds the project")


def test_find_private_matches_flags_offenders_and_skips_clean():
  files = {
    "bad.md": PRIVATE_CASES[0][0],
    "good.md": b"\n".join(neg for _, neg in PRIVATE_CASES),
    "skipped.bin": b"\x00" + PRIVATE_CASES[0][0],
  }
  offenders = git_hooks.find_private_matches(
    sorted(files), reader_for(files), git_hooks.PRIVATE_PATTERNS
  )
  assert offenders == ["bad.md"]


def test_find_private_matches_checks_text_regardless_of_filename():
  files = {
    "CMakeLists.txt": PRIVATE_CASES[0][0],
    "Dockerfile": PRIVATE_CASES[0][0],
    "LICENSE": PRIVATE_CASES[0][0],
    "config.h.in": PRIVATE_CASES[0][0],
    "script.sh": PRIVATE_CASES[0][0],
    "generated.lock": PRIVATE_CASES[0][0],
  }

  assert git_hooks.find_private_matches(
    sorted(files), reader_for(files), git_hooks.PRIVATE_PATTERNS
  ) == sorted(files)


def test_find_private_matches_checks_filename_even_for_binary_data():
  private_name = "notes/person" + "@example.com/archive.bin"
  files = {private_name: b"\x00binary"}

  assert git_hooks.find_private_matches(
    files, reader_for(files), git_hooks.PRIVATE_PATTERNS
  ) == [private_name]


def test_find_conflict_markers_detects_each_marker_kind():
  marker_lines = (
    b"<<<" + b"<<<< HEAD",
    b">>>" + b">>>> theirs",
    b"|||" + b"|||| base",
    b"===" + b"====",
  )
  for line in marker_lines:
    files = {"a.md": b"text\n" + line + b"\nmore\n"}
    offenders = git_hooks.find_conflict_markers(["a.md"], reader_for(files))
    assert offenders == ["a.md"], line


def test_find_conflict_markers_checks_generated_text_lockfiles():
  marker = b"<<<" + b"<<<< HEAD"
  files = {"generated.lock": b"version = 1\n" + marker + b"\n"}

  assert git_hooks.find_conflict_markers(files, reader_for(files)) == [
    "generated.lock"
  ]


def test_find_conflict_markers_ignores_near_misses_and_binaries():
  files = {
    "a.md": b"====== =\nx <<<" + b"<<<< y\n====" + b"====\n",
    "b.bin": b"\x00<<<" + b"<<<< HEAD\n",
  }
  offenders = git_hooks.find_conflict_markers(sorted(files), reader_for(files))
  assert offenders == []


def test_find_token_overruns_flags_only_oversized_text_files():
  tiktoken = pytest.importorskip("tiktoken")
  encoder = tiktoken.encoding_for_model("gpt-5")
  files = {
    "big.md": b"word " * 30_000,
    "small.md": b"short file\n",
    "big.bin": b"\x00" + b"word " * 30_000,
  }
  overruns = git_hooks.find_token_overruns(
    sorted(files), reader_for(files), encoder
  )
  assert [path for path, _ in overruns] == ["big.md"]
  assert overruns[0][1] > git_hooks.TOKEN_LIMIT


def test_find_token_overruns_rejects_exact_limit():
  class ExactLimitEncoder:
    def encode(self, _text):
      return range(git_hooks.TOKEN_LIMIT)

  files = {"exact.md": b"content"}

  assert git_hooks.find_token_overruns(
    files, reader_for(files), ExactLimitEncoder()
  ) == [("exact.md", git_hooks.TOKEN_LIMIT)]


def test_find_token_overruns_replaces_malformed_utf8():
  class CapturingEncoder:
    text = ""

    def encode(self, text):
      self.text = text
      return ()

  encoder = CapturingEncoder()

  assert (
    git_hooks.find_token_overruns(
      ["malformed.md"],
      reader_for({"malformed.md": b"before\xffafter"}),
      encoder,
    )
    == []
  )
  assert encoder.text == "before\ufffdafter"


def test_token_encoder_matches_the_gpt5_cli_model():
  tiktoken = pytest.importorskip("tiktoken")

  assert git_hooks.token_encoder().name == "o200k_base"
  assert git_hooks.token_encoder().name == (
    tiktoken.encoding_for_model("gpt-5").name
  )


def test_index_paths_and_blobs_are_nul_safe_and_do_not_follow_symlinks(
  tmp_path,
):
  repo = tmp_path / "repo"
  repo.mkdir()
  subprocess.run(["git", "init", "-q", str(repo)], check=True)
  newline_name = "line\nbreak.md"
  (repo / newline_name).write_text("safe\n", encoding="utf-8")
  outside = tmp_path / "outside.txt"
  outside.write_bytes(PRIVATE_CASES[0][0])
  link = repo / "outside-link"
  try:
    link.symlink_to(Path("..") / outside.name)
  except OSError as error:
    pytest.skip(f"symlinks unavailable: {error}")
  subprocess.run(
    ["git", "-C", str(repo), "add", newline_name, link.name],
    check=True,
  )

  assert git_hooks.tracked_files(repo) == [newline_name, link.name]
  assert git_hooks.staged_files(repo) == [newline_name, link.name]
  blobs = git_hooks.read_index_blobs([newline_name, link.name], repo_root=repo)
  assert blobs[newline_name] == b"safe\n"
  assert blobs[link.name] == os.fsencode("../outside.txt")
  assert PRIVATE_CASES[0][0] not in blobs[link.name]


def test_index_blob_read_fails_closed_for_a_missing_path(tmp_path):
  repo = tmp_path / "repo"
  repo.mkdir()
  subprocess.run(["git", "init", "-q", str(repo)], check=True)

  with pytest.raises(git_hooks.RepositoryReadError, match="missing.md"):
    git_hooks.read_index_blobs(["missing.md"], repo_root=repo)


def test_staged_paths_include_type_changes(monkeypatch):
  def fake_git_bytes(argv, repo_root):
    assert argv == [
      "diff",
      "--cached",
      "--name-only",
      "--diff-filter=ACMRT",
      "-z",
    ]
    assert repo_root == git_hooks.REPO_ROOT
    return b"changed-link\0"

  monkeypatch.setattr(git_hooks, "git_bytes", fake_git_bytes)

  assert git_hooks.staged_files() == ["changed-link"]


def test_diff_paths_uses_nul_delimiters(monkeypatch):
  def fake_git_bytes(argv, repo_root):
    assert argv == ["diff", "--name-only", "-z", "base", "HEAD"]
    assert repo_root == git_hooks.REPO_ROOT
    return b"docs/line\nbreak.md\0bench/tess.cc\0"

  monkeypatch.setattr(git_hooks, "git_bytes", fake_git_bytes)

  assert git_hooks.diff_paths("base", "HEAD") == [
    "docs/line\nbreak.md",
    "bench/tess.cc",
  ]


def test_display_path_escapes_terminal_control_characters():
  displayed = git_hooks.display_path("line\n\x1b[31mname")

  assert displayed == "'line\\n\\x1b[31mname'"
  assert "\n" not in displayed
  assert "\x1b" not in displayed


def test_uv_dev_command_uses_compiled_requirements_without_project():
  command = git_hooks.uv_dev_command("uv-bin", "python", "tool.py")

  assert command == [
    "uv-bin",
    "run",
    "--no-project",
    "--with-requirements",
    str(git_hooks.DEV_REQUIREMENTS),
    "--",
    "python",
    "tool.py",
  ]


@pytest.mark.parametrize(
  ("email", "expected"),
  [
    ("123+example" + "@users.noreply.github.com", True),
    ("example" + "@users.noreply.github.com", True),
    ("person" + "@example.com", False),
    ("123+bad" + "@example.com", False),
  ],
)
def test_github_noreply_email_accepts_current_and_legacy_forms(email, expected):
  assert git_hooks.is_github_noreply_email(email) is expected


@pytest.mark.parametrize("returncode", [1, 129])
def test_config_hooks_require_a_successful_probe(monkeypatch, returncode):
  result = subprocess.CompletedProcess(
    args=["git", "hook", "list", "pre-commit"],
    returncode=returncode,
    stdout="unsupported\n",
  )
  monkeypatch.setattr(git_hooks, "run", lambda *args, **kwargs: result)

  assert git_hooks.supports_config_hooks() is False


def test_config_hooks_accept_a_successful_probe(monkeypatch):
  result = subprocess.CompletedProcess(
    args=["git", "hook", "list", "pre-commit"],
    returncode=0,
    stdout="",
  )
  monkeypatch.setattr(git_hooks, "run", lambda *args, **kwargs: result)

  assert git_hooks.supports_config_hooks() is True


def test_every_checkout_step_disables_persisted_credentials():
  root = Path(__file__).resolve().parents[1]
  workflows = tuple((root / ".github" / "workflows").glob("*.yml"))
  workflow_text = "\n".join(path.read_text() for path in workflows)
  checkout_count = workflow_text.count("uses: actions/checkout@")

  assert checkout_count > 0
  assert workflow_text.count("persist-credentials: false") == checkout_count


def test_documented_checkout_version_matches_workflows():
  root = Path(__file__).resolve().parents[1]
  workflows = tuple((root / ".github" / "workflows").glob("*.yml"))
  workflow_text = "\n".join(path.read_text() for path in workflows)
  checkout_re = re.compile(
    r"uses: actions/checkout@([0-9a-f]{40}) # (v[0-9.]+)"
  )
  checkout_pins = set(checkout_re.findall(workflow_text))

  assert len(checkout_pins) == 1
  revision, version = checkout_pins.pop()
  dependencies = (root / "docs" / "dependencies.md").read_text()
  documented_version = f"Checkout action version: `actions/checkout@{version}`"
  assert documented_version in dependencies
  assert f"`{revision}`" in dependencies


def test_hook_backstop_uses_first_party_python_and_requires_hashes():
  root = Path(__file__).resolve().parents[1]
  workflow = (root / ".github" / "workflows" / "ci.yml").read_text()
  assert "permissions:\n  actions: read\n  contents: read\n" in workflow

  assert (
    "uses: actions/setup-python@"
    "5fda3b95a4ea91299a34e894583c3862153e4b97" in workflow
  )
  assert 'python-version: "3.12"' in workflow
  assert "--require-hashes" in workflow
  assert "--requirement requirements-dev.txt" in workflow


def _job_body(workflow: str, job_id: str) -> str:
  """Return one job's block, bounded by the next job header.

  Splitting on the job header alone returns the rest of the FILE, so an
  assertion meant for this job can be satisfied by a later one -- which
  is what happened here: the required-jobs block below was matching
  `report-failure`'s identical `needs:` list, so `ci-gate` could have
  dropped a job without failing this test.
  """
  body = workflow.split(f"  {job_id}:\n", 1)[1]
  following = re.search(r"^  [a-z0-9][a-z0-9_-]*:$", body, flags=re.M)
  return body[: following.start()] if following else body


def _workflow_run_source(source: str) -> str:
  """Strip exactly the YAML block indentation from embedded source."""
  lines = source.splitlines()
  assert all(not line or line.startswith("          ") for line in lines)
  return "\n".join(line.removeprefix("          ") for line in lines)


def _reportable_ci_failures(job_results: dict[str, dict[str, str]]) -> str:
  """Execute the failure classifier embedded in the reporter job."""
  root = Path(__file__).resolve().parents[1]
  workflow = (root / ".github/workflows/ci.yml").read_text()
  reporter = _job_body(workflow, "report-failure")
  match = re.search(
    r"failed=\$\(printf '%s' \"\$JOB_RESULTS\" \| python3 -c '\n"
    r"(?P<source>.*?)\n\s*'\)",
    reporter,
    flags=re.S,
  )
  assert match is not None
  source = _workflow_run_source(match.group("source"))
  result = subprocess.run(
    (sys.executable, "-c", source),
    input=json.dumps(job_results),
    check=True,
    capture_output=True,
    text=True,
  )
  return result.stdout.strip()


def _ci_run_is_superseded(
  runs: dict[str, list[dict[str, object]]],
  current_run_number: int,
  event: str = "push",
) -> bool:
  """Execute the reporter's exact newer-equivalent-run classifier."""
  root = Path(__file__).resolve().parents[1]
  workflow = (root / ".github/workflows/ci.yml").read_text()
  reporter = _job_body(workflow, "report-failure")
  start = "# CI_SUPERSESSION_CLASSIFIER_BEGIN\n"
  end = "# CI_SUPERSESSION_CLASSIFIER_END"
  assert start in reporter
  source = _workflow_run_source(
    reporter.split(start, 1)[1].split(end, 1)[0]
  )
  env = {
    **os.environ,
    "CURRENT_RUN_NUMBER": str(current_run_number),
    "GITHUB_EVENT_NAME": event,
    "GITHUB_REF_NAME": "main",
  }
  result = subprocess.run(
    (sys.executable, "-c", source),
    input=json.dumps(runs),
    check=True,
    capture_output=True,
    text=True,
    env=env,
  )
  assert result.stdout.strip() in ("false", "true")
  return result.stdout.strip() == "true"


def test_ci_gate_aggregates_every_required_ci_job():
  root = Path(__file__).resolve().parents[1]
  workflow = (root / ".github" / "workflows" / "ci.yml").read_text()
  ci_gate = _job_body(workflow, "ci-gate")
  required_jobs = (
    "changes",
    "dev",
    "gcc",
    "libcxx",
    "no-exceptions",
    "hooks-backstop",
    "quality",
    "tidy-diff",
    "macos",
    "windows",
    "windows-noexceptions",
    "bench",
  )
  needs = "    needs:\n" + "".join(
    f"      - {job_id}\n" for job_id in required_jobs
  )

  assert "    name: CI Gate\n" in ci_gate
  assert "    if: ${{ always() }}\n" in ci_gate
  assert needs in ci_gate
  assert "      - advisory\n" not in ci_gate
  for job_id in required_jobs:
    result_check = f'test "${{{{ needs.{job_id}.result }}}}" = success'
    assert result_check in ci_gate

  report_failure = _job_body(workflow, "report-failure")
  classifier = re.search(
    r"gate_jobs = \((?P<jobs>.*?)\n\s*\)",
    report_failure,
    flags=re.S,
  )
  assert classifier is not None
  for job_id in ("no-exceptions", "windows-noexceptions"):
    assert f"      - {job_id}\n" in report_failure
  assert tuple(re.findall(r'"([a-z0-9_-]+)"', classifier.group("jobs"))) == (
    required_jobs
  )


def test_ci_failure_reporter_ignores_superseded_gate_failure():
  results = {
    "ci-gate": {"result": "failure"},
    "quality": {"result": "cancelled"},
    "dev": {"result": "success"},
  }

  assert _reportable_ci_failures(results) == ""

  root = Path(__file__).resolve().parents[1]
  workflow = (root / ".github/workflows/ci.yml").read_text()
  reporter = _job_body(workflow, "report-failure")
  guard = reporter.index('if [ -z "$failed" ]; then')
  api_lookup = reporter.index("actions/workflows/ci.yml/runs", guard)
  confirmed = reporter.index('if [ "$superseded" = true ]', api_lookup)
  exit_guard = reporter.index("exit 0", confirmed)
  fail_closed = reporter.index("failed=ci-gate", exit_guard)
  issue_body = reporter.index('title="CI failing on', fail_closed)
  assert guard < api_lookup < confirmed < exit_guard < fail_closed < issue_body
  assert "    permissions:\n      actions: read\n" in reporter
  assert '-f branch="$GITHUB_REF_NAME"' in reporter
  assert '-f event="$GITHUB_EVENT_NAME"' in reporter
  assert 'if [ "$GITHUB_EVENT_NAME" = push ] &&' in reporter


def test_ci_failure_reporter_confirms_newer_equivalent_run():
  runs = {
    "workflow_runs": [
      {"event": "push", "head_branch": "main", "run_number": 42},
      {"event": "push", "head_branch": "main", "run_number": 41},
    ]
  }

  assert _ci_run_is_superseded(runs, 41)
  assert not _ci_run_is_superseded(runs, 42)


def test_ci_failure_reporter_rejects_newer_unrelated_run():
  runs = {
    "workflow_runs": [
      {"event": "push", "head_branch": "other", "run_number": 44},
      {"event": "schedule", "head_branch": "main", "run_number": 43},
      {"event": "push", "head_branch": "main", "run_number": 41},
    ]
  }

  assert not _ci_run_is_superseded(runs, 42)


def test_ci_failure_reporter_rejects_newer_dispatch_with_unknown_key():
  runs = {
    "workflow_runs": [
      {
        "event": "workflow_dispatch",
        "head_branch": "main",
        "run_number": 43,
      },
    ]
  }

  # The run-list API does not expose inputs.expected_sha, so it cannot prove
  # that two manual release runs share the workflow concurrency key.
  assert not _ci_run_is_superseded(runs, 42, event="workflow_dispatch")


def test_ci_failure_reporter_keeps_timeout_without_newer_run():
  results = {
    "ci-gate": {"result": "failure"},
    "quality": {"result": "cancelled"},
  }
  runs = {
    "workflow_runs": [
      {"event": "push", "head_branch": "main", "run_number": 42},
    ]
  }

  # A cancelled required job is only a candidate for supersession. Without
  # explicit evidence of a newer equivalent run, reporting remains enabled.
  assert _reportable_ci_failures(results) == ""
  assert not _ci_run_is_superseded(runs, 42)


def test_ci_failure_reporter_keeps_real_failure_during_cancellation():
  results = {
    "ci-gate": {"result": "failure"},
    "quality": {"result": "failure"},
    "bench": {"result": "cancelled"},
  }

  assert _reportable_ci_failures(results) == "ci-gate, quality"


def test_ci_failure_reporter_keeps_gate_logic_failure():
  results = {
    "ci-gate": {"result": "failure"},
    "quality": {"result": "success"},
    "dev": {"result": "success"},
  }

  assert _reportable_ci_failures(results) == "ci-gate"


def test_ci_failure_reporter_does_not_mistake_advisory_cancellation():
  results = {
    "ci-gate": {"result": "failure"},
    "quality": {"result": "success"},
    "bench-baselines": {"result": "cancelled"},
  }

  assert _reportable_ci_failures(results) == "ci-gate"


# Jobs deliberately outside the merge gate, each with the reason it is
# advisory. `CI Gate` is one of only two required checks, so a job absent
# from both this waiver and the gate's `needs` is non-blocking by
# accident — which the previous hardcoded job tuple could not detect.
ADVISORY_CI_JOBS = {
  "ci-gate": "the gate itself",
  "paired-bench": "shadow mode; the section 4.3 promotion criteria are unmet",
  "bench-baselines": (
    "non-gating baseline collection after a main push; feeds change-point "
    "and publish-benchmark-history, never the merge decision"
  ),
  "change-point": "post-merge drift detection on main, not a pull-request gate",
  "publish-benchmark-history": "publishes baselines after a main push",
  "long-seed-properties": "scheduled deep sweep, far longer than a gate allows",
  "coverage": "weekly advisory gap-finder, not a threshold",
  "release-linux-floors": "main/release floor; release-evidence governs RCs",
  "release-macos-floor": "main/release floor; release-evidence governs RCs",
  "release-windows-floor": "main/release floor; release-evidence governs RCs",
  "release-cmake-floor": "main/release floor; release-evidence governs RCs",
  "release-fuzz": "scheduled/release fuzz; release-evidence governs RCs",
  "release-packages": "release-only; governed by release-evidence",
  "release-compatibility": "release-only; governed by release-evidence",
  "release-docs": "release-only; governed by release-evidence",
  "release-evidence": "release-only aggregate gate",
  "report-failure": "reports other jobs' failures; gating on it is circular",
}


def test_every_ci_job_is_gated_or_explicitly_waived():
  """Derives the job set from the workflow instead of restating it.

  The gate-integrity check used to assert against a hardcoded tuple, so
  any job added later was non-blocking by default and no test said so.
  """
  root = Path(__file__).resolve().parents[1]
  workflow = (root / ".github" / "workflows" / "ci.yml").read_text()
  body = workflow.split("\njobs:\n", 1)[1]
  jobs = re.findall(r"^  ([a-z0-9][a-z0-9_-]*):$", body, flags=re.M)

  assert len(jobs) >= 15, jobs
  ci_gate = _job_body(workflow, "ci-gate")
  gated = set()
  for line in ci_gate.split("    needs:\n", 1)[1].split("\n"):
    entry = re.match(r"^      - ([a-z0-9][a-z0-9_-]*)$", line)
    if not entry:
      break  # first line that is not a needs item ends the block
    gated.add(entry.group(1))

  ungoverned = [
    job for job in jobs if job not in gated and job not in ADVISORY_CI_JOBS
  ]
  assert ungoverned == [], (
    f"jobs neither gated nor waived: {ungoverned}. Add them to ci-gate's "
    "needs with a result check, or to ADVISORY_CI_JOBS with a reason."
  )

  # A waiver must not silently cover a job that is in fact gated.
  contradictory = [job for job in gated if job in ADVISORY_CI_JOBS]
  assert contradictory == []

  for job in gated:
    assert f'test "${{{{ needs.{job}.result }}}}" = success' in ci_gate
    assert job in jobs, f"ci-gate needs {job}, which is not a job"

  for reason in ADVISORY_CI_JOBS.values():
    assert reason.strip()


def test_release_mode_requires_exact_identity_and_aggregates_every_gate():
  root = Path(__file__).resolve().parents[1]
  workflow = (root / ".github" / "workflows" / "ci.yml").read_text()
  changes = _job_body(workflow, "changes")
  evidence = _job_body(workflow, "release-evidence")
  release_jobs = (
    "release-linux-floors",
    "release-macos-floor",
    "release-windows-floor",
    "release-cmake-floor",
    "release-fuzz",
    "release-packages",
    "release-compatibility",
    "release-docs",
  )
  always_required = (
    "dev",
    "gcc",
    "libcxx",
    "no-exceptions",
    "hooks-backstop",
    "quality",
    "macos",
    "windows",
    "windows-noexceptions",
    "bench",
    "long-seed-properties",
    "coverage",
    "ci-gate",
  ) + release_jobs

  assert "ref:" in workflow.split("workflow_dispatch:", 1)[1]
  assert "expected_version:" in workflow.split("workflow_dispatch:", 1)[1]
  assert "expected_sha:" in workflow.split("workflow_dispatch:", 1)[1]
  assert 'test "$actual_sha" = "$EXPECTED_SHA"' in changes
  assert 'test "$version" = "$EXPECTED_VERSION"' in changes
  assert "needs.changes.outputs.release_mode == 'true'" in evidence
  for job in always_required:
    assert f"      - {job}\n" in evidence
  assert "contains(needs.*.result" not in evidence
  assert 'test "$result" = success' in evidence
  assert "            release-evidence.json\n" in evidence
  assert "            compatibility/\n" in evidence
  assert '"workflow_run_url": os.environ["WORKFLOW_RUN_URL"]' in evidence
  assert '"bundled_job_logs": observed_logs' in evidence
  assert "release-job-logs/" in evidence
  assert "release-jobs-pages.json" in evidence
  assert "hashlib.sha256" in evidence
  assert '"vcpkg_commit":' in evidence
  assert '"clang_tidy": "18"' in evidence

  windows_floor = _job_body(workflow, "release-windows-floor")
  assert "    runs-on: windows-2022\n" in windows_floor
  assert "if ($null -eq $file)" in windows_floor
  assert "Select-String" in windows_floor
  assert "-Quiet" in windows_floor
  assert "if (-not $versionMatches)" in windows_floor
  assert windows_floor.count("throw ") >= 2

  for job in release_jobs[5:]:
    body = _job_body(workflow, job)
    assert "needs.changes.outputs.release_mode == 'true'" in body

  for job in release_jobs[:4]:
    body = _job_body(workflow, job)
    assert "needs.changes.outputs.code_required == 'true'" in body
    assert "github.event_name != 'pull_request'" in body

  fuzz = _job_body(workflow, "release-fuzz")
  assert "needs.changes.outputs.release_mode == 'true'" in fuzz
  assert "github.event_name == 'schedule'" in fuzz

  fuzzer = (root / "tests/fuzz/tess_world_archive_fuzzer.cc").read_text(
      encoding="utf-8"
  )
  assert "normalize_archive_envelope" in fuzzer
  assert "detail::archive_crc32" in fuzzer
  assert fuzzer.count("inspect_world_archive") == 2
  assert fuzzer.count("load_world_archive<Archive>") == 2

  compatibility = _job_body(workflow, "release-compatibility")
  assert "fetch-depth: 0" in compatibility
  assert "fetch-tags: true" in compatibility
  assert "cmake --install build/compatibility" in compatibility
  assert '-DCMAKE_PREFIX_PATH="$install_prefix"' in compatibility
  assert 'cmake -S "$snapshot/$consumer_project"' in compatibility
  assert 'cmake --build "build/compatibility/$version-consumer"' in (
      compatibility
  )
  assert 'ctest --test-dir "build/compatibility/$version-consumer"' in (
      compatibility
  )
  assert compatibility.count("--no-tests=error") == 2
  assert 'consumer_target="$(jq -r .consumer_target "$manifest")"' in (
      compatibility
  )
  assert 'archive_consumer_target="$(' in compatibility
  assert 'jq -r .archive_consumer_target "$manifest"' in compatibility
  assert '-R "^$consumer_target$"' in compatibility
  assert '-R "^$archive_consumer_target$"' in compatibility
  assert "c++ -std=c++20 -Iinclude" not in compatibility

  hooks = _job_body(workflow, "hooks-backstop")
  assert "fetch-depth: 0" in hooks
  assert "fetch-tags: true" in hooks
  assert "python tools/check_compatibility_snapshots.py" in hooks

  packages = _job_body(workflow, "release-packages")
  assert "CMAKE_CXX_COMPILER_LAUNCHER: \"\"" in packages
  assert "conan create . --build=missing -s compiler.cppstd=20" in packages

  # These jobs do not provision ccache. A command-line cache override is too
  # late for CMake's first compiler probe when the workflow-level environment
  # already names the absent launcher, so each job must shadow it explicitly.
  ccache_free_jobs = (
    "release-macos-floor",
    "release-windows-floor",
    "release-cmake-floor",
    "release-fuzz",
    "release-packages",
    "release-compatibility",
    "release-docs",
  )
  for job in ccache_free_jobs:
    body = _job_body(workflow, job)
    assert '    env:\n      CMAKE_CXX_COMPILER_LAUNCHER: ""\n' in body


def test_release_macos_floor_bounds_build_parallelism():
  """The three-core hosted runner must not launch unbounded compilers."""
  root = Path(__file__).resolve().parents[1]
  workflow = (root / ".github" / "workflows" / "ci.yml").read_text()
  macos_floor = _job_body(workflow, "release-macos-floor")

  assert (
    "cmake --build build/release-macos-floor --parallel 3" in macos_floor
  )


def test_non_pr_failure_report_observes_every_runnable_job():
  """Every non-PR job failure must reach the rolling failure issue."""
  root = Path(__file__).resolve().parents[1]
  workflow = (root / ".github" / "workflows" / "ci.yml").read_text()
  body = workflow.split("\njobs:\n", 1)[1]
  jobs = set(
    re.findall(r"^  ([a-z0-9][a-z0-9_-]*):$", body, flags=re.M)
  )
  report_failure = _job_body(workflow, "report-failure")
  reported = set()
  for line in report_failure.split("    needs:\n", 1)[1].split("\n"):
    entry = re.match(r"^      - ([a-z0-9][a-z0-9_-]*)$", line)
    if not entry:
      break
    reported.add(entry.group(1))

  # paired-bench is pull-request-only, while the reporter is deliberately
  # disabled on pull requests. The reporter cannot depend on itself.
  expected_unreported = {"paired-bench", "report-failure"}
  assert jobs - reported == expected_unreported


def test_documentation_only_changes_skip_expensive_ci_fail_closed():
  root = Path(__file__).resolve().parents[1]
  workflow = (root / ".github" / "workflows" / "ci.yml").read_text()
  expensive_jobs = (
    "dev",
    "gcc",
    "no-exceptions",
    "quality",
    "windows",
    "windows-noexceptions",
    "bench",
  )

  assert "  changes:\n    name: Classify Changes\n" in workflow
  assert "      code_required: ${{ steps.classify.outputs.code_required }}\n" in (
    workflow
  )
  assert '          fetch-depth: 0\n' in workflow
  assert '        id: classify\n' in workflow
  assert (
    '          PR_BASE_SHA: ${{ github.event.pull_request.base.sha }}\n'
    in workflow
  )
  assert '          PUSH_BASE_SHA: ${{ github.event.before }}\n' in workflow
  assert '          HEAD_SHA: ${{ github.sha }}\n' in workflow
  assert "          python3 tools/ci_changes.py\n" in workflow
  assert '          "${PR_BASE_SHA:-$PUSH_BASE_SHA}" "$HEAD_SHA"\n' in workflow
  assert '          --event "$EVENT_NAME"\n' in workflow
  assert '          >> "$GITHUB_OUTPUT"\n' in workflow
  for job_id in expensive_jobs:
    assert (
      f"  {job_id}:\n"
      "    needs: changes\n"
      "    if: ${{ needs.changes.outputs.code_required == 'true' }}\n"
      in workflow
    )

  no_exceptions = workflow.split("  no-exceptions:\n", 1)[1].split(
    "  hooks-backstop:\n", 1
  )[0]
  assert "name: Clang ASan UBSan" in no_exceptions
  assert "name: GCC Werror" in no_exceptions
  assert "tess_no_exceptions_test" in no_exceptions
  assert "tess_no_exceptions_headers_verify_interface_header_sets" in (
    no_exceptions
  )
  assert "tess_no_exceptions_contract_cells" in no_exceptions
  assert "cmake --preset examples-no-exceptions" in no_exceptions
  assert "TESS_BUILD_NO_EXCEPTIONS_TESTING=ON" in no_exceptions
  assert no_exceptions.count("TESS_NO_EXCEPTIONS: 1") == 2

  windows = workflow.split("  windows:\n", 1)[1].split(
    "  windows-noexceptions:\n", 1
  )[0]
  assert "TESS_BUILD_NO_EXCEPTIONS_TESTING=ON" not in windows
  assert "Build targeted exception-free contracts" not in windows

  windows_noexceptions = workflow.split(
    "  windows-noexceptions:\n", 1
  )[1].split("  bench:\n", 1)[0]
  assert "Build targeted exception-free contracts" in windows_noexceptions
  assert "tess_no_exceptions_test" in windows_noexceptions
  assert "tess_no_exceptions_headers_verify_interface_header_sets" in (
    windows_noexceptions
  )
  assert "tess_no_exceptions_contract_cells" in windows_noexceptions
  assert "-L config:noexceptions" in windows_noexceptions
  assert "TESS_BUILD_NO_EXCEPTIONS_TESTING=ON" in windows_noexceptions
  assert (
    "cmake --build build/windows-msvc --config Debug --parallel"
    in windows_noexceptions
  )
  assert (
    "ctest --test-dir build/windows-msvc -C Debug" in windows_noexceptions
  )
  assert (
    "--ctest-dir build/windows-msvc --config Debug" in windows_noexceptions
  )
  assert windows_noexceptions.count("TESS_NO_EXCEPTIONS: 1") == 2
  for target in (
    "tess_block_test",
    "tess_consumer_contract_test",
    "tess_maintenance_test",
    "tess_path_cache_test",
    "tess_phase_executor_test",
    "tess_sim_auto_exec_test",
    "tess_sim_schedule_test",
    "tess_storage_test",
    "tess_topology_test",
  ):
    assert target in windows_noexceptions

  # Tier-conditional jobs also require code_required, fail closed.
  assert (
    "  tidy-diff:\n"
    "    needs: changes\n"
    "    if: >-\n"
    "      ${{ github.event_name == 'pull_request' &&\n"
    "          needs.changes.outputs.code_required == 'true' }}\n"
    in workflow
  )
  assert (
    "  macos:\n"
    "    needs: changes\n"
    "    if: >-\n"
    "      ${{ github.event_name != 'pull_request' &&\n"
    "          needs.changes.outputs.code_required == 'true' }}\n"
    in workflow
  )

  assert "  hooks-backstop:\n    name: Hook Backstop Checks\n" in workflow
  assert "          tests/test_ci_changes.py\n" in workflow

  ci_gate = workflow.split("  ci-gate:\n", 1)[1]
  assert "      - changes\n" in ci_gate
  assert 'test "${{ needs.changes.result }}" = success' in ci_gate
  assert (
    'code="${{ needs.changes.outputs.code_required }}"\n' in ci_gate
  )
  assert 'test "$code" = true || test "$code" = false' in ci_gate
  for job_id in expensive_jobs:
    assert (
      f'test "${{{{ needs.{job_id}.result }}}}" = success' in ci_gate
    )
    assert (
      f'test "${{{{ needs.{job_id}.result }}}}" = skipped' in ci_gate
    )


def test_noisy_clang_tidy_runs_off_the_per_commit_workflow():
  root = Path(__file__).resolve().parents[1]
  ci_workflow = (root / ".github" / "workflows" / "ci.yml").read_text()
  advisory_workflow = (
    root / ".github" / "workflows" / "advisory.yml"
  ).read_text()

  assert "dev-clang-tidy-advisory" not in ci_workflow
  assert "  schedule:\n" in advisory_workflow
  assert "  workflow_dispatch:\n" in advisory_workflow
  assert "cmake --preset dev-clang-tidy-advisory" in advisory_workflow
  assert "cmake --build --preset dev-clang-tidy-advisory" in (
    advisory_workflow
  )


def test_required_clang_tidy_uses_bounded_parallelism():
  root = Path(__file__).resolve().parents[1]
  workflow = (root / ".github" / "workflows" / "ci.yml").read_text()
  quality = workflow.split("  quality:\n", 1)[1].split("  macos:\n", 1)[0]

  assert (
    "      - name: Build\n"
    "        if: matrix.preset != 'dev-clang-tidy'\n"
    '        run: cmake --build --preset "${{ matrix.preset }}"\n'
    in quality
  )
  assert "      - name: Build clang-tidy with bounded parallelism\n" in quality
  assert "        if: matrix.preset == 'dev-clang-tidy'\n" in quality
  assert (
    '        run: cmake --build --preset "${{ matrix.preset }}" '
    "--parallel 4\n" in quality
  )


def test_required_clang_tidy_uses_an_explicit_major_version():
  root = Path(__file__).resolve().parents[1]
  workflow = (root / ".github" / "workflows" / "ci.yml").read_text()
  quality = workflow.split("  quality:\n", 1)[1].split("  macos:\n", 1)[0]
  tidy_diff = workflow.split("  tidy-diff:\n", 1)[1].split(
    "  macos:\n", 1
  )[0]

  assert "sudo apt-get install -y clang-tidy-18" in quality
  assert "clang-tidy-18 --version" in quality
  assert "-DTESS_CLANG_TIDY_EXE=clang-tidy-18" in quality
  assert "sudo apt-get install -y ccache clang-tidy-18" in tidy_diff
  assert "clang-tidy-18 --version" in tidy_diff
  assert "--clang-tidy clang-tidy-18" in tidy_diff

  # The advisory profile is schedule-only, so an unpinned install there
  # changes meaning with the runner image and no pull request would flag
  # it. It ran `apt-get install -y ccache clang-tidy` until 2026-08-07.
  advisory = (root / ".github" / "workflows" / "advisory.yml").read_text()
  assert "sudo apt-get install -y ccache clang-tidy-18" in advisory
  assert "clang-tidy-18 --version" in advisory
  assert "-DTESS_CLANG_TIDY_EXE=clang-tidy-18" in advisory


def test_diff_clang_tidy_timeout_covers_a_large_public_surface_change():
  root = Path(__file__).resolve().parents[1]
  workflow = (root / ".github" / "workflows" / "ci.yml").read_text()
  tidy_diff = _job_body(workflow, "tidy-diff")

  assert "    timeout-minutes: 30\n" in tidy_diff


def test_non_gating_benchmark_baselines_run_only_on_main():
  """Baselines live in their own main-push-only job, outside the gate.

  They used to be trailing steps of the gates job, where their
  ~27-minute run pushed it past the 45-minute ceiling on every full
  main push — cancelling CI Gate while the threshold gates themselves
  were green. The job-level guard replaces the per-step guards.
  """
  root = Path(__file__).resolve().parents[1]
  workflow = (root / ".github" / "workflows" / "ci.yml").read_text()
  baselines = _job_body(workflow, "bench-baselines")
  bench = _job_body(workflow, "bench")
  moved_steps = (
    "Collect non-gating benchmark baselines",
    "Write benchmark artifact metadata",
    "Upload benchmark baseline artifact",
  )

  # Anchored: the guard must be the JOB-level if, not a comment or a
  # step condition.
  job_guard = (
    "    if: >-\n"
    "      ${{ github.event_name == 'push' &&\n"
    "          github.ref == 'refs/heads/main' &&\n"
    "          needs.changes.outputs.code_required == 'true' }}\n"
  )
  assert job_guard in baselines
  assert "    needs: changes\n" in baselines
  for name in moved_steps:
    assert f"      - name: {name}\n" in baselines
    assert f"      - name: {name}\n" not in bench

  thresholds = bench.split("      - name: Benchmark thresholds\n", 1)[1]
  assert thresholds.startswith(
    "        if: github.event_name != 'pull_request'\n"
  )


def test_baseline_overruns_conclude_as_failures_report_files_them():
  """A job-ceiling kill concludes `cancelled` — indistinguishable from a
  benign concurrency supersede, and invisible to report-failure, which
  matches only `failure`. The step-level timeouts are what convert an
  overrun into a reportable failure, so they must cover every long step
  and sum below the job ceiling; report-failure must observe the job.
  """
  root = Path(__file__).resolve().parents[1]
  workflow = (root / ".github" / "workflows" / "ci.yml").read_text()
  baselines = _job_body(workflow, "bench-baselines")

  assert "    timeout-minutes: 75\n" in baselines
  build = baselines.split("      - name: Build\n", 1)[1]
  assert build.startswith("        timeout-minutes: 30\n")
  collect = baselines.split(
    "      - name: Collect non-gating benchmark baselines\n", 1
  )[1]
  assert collect.startswith("        timeout-minutes: 35\n")
  # 30 + 35 + setup fits under 75: the ceiling is a backstop that never
  # fires first.
  assert 30 + 35 < 75

  report_failure = _job_body(workflow, "report-failure")
  assert "      - bench-baselines\n" in report_failure


def test_baseline_artifact_wiring_survives_the_job_split():
  """Both consumers must wait on bench-baselines (not the gates job),
  the publisher must gate on its success, and the artifact name must
  match between the one upload and both downloads."""
  root = Path(__file__).resolve().parents[1]
  workflow = (root / ".github" / "workflows" / "ci.yml").read_text()
  baselines = _job_body(workflow, "bench-baselines")
  change_point = _job_body(workflow, "change-point")
  publisher = _job_body(workflow, "publish-benchmark-history")

  assert "    needs: bench-baselines\n" in change_point
  assert "    needs: bench-baselines\n" in publisher
  assert "needs.bench-baselines.result == 'success' }}" in publisher

  artifact = "benchmark-baselines-${{ github.run_id }}"
  assert f"          name: {artifact}\n" in baselines
  assert f"          name: {artifact}\n" in publisher
  # change-point lists artifacts by prefix instead of exact name.
  assert 'startswith("benchmark-baselines-")' in change_point


def test_pages_build_has_only_the_permissions_needed_to_configure_pages():
  root = Path(__file__).resolve().parents[1]
  workflow = (root / ".github" / "workflows" / "pages.yml").read_text()
  build_job = workflow.split("  build:\n", 1)[1].split("  deploy:\n", 1)[0]

  assert "    permissions:\n      contents: read\n      pages: read\n" in build_job
  assert "      id-token: write" not in build_job


def test_pages_build_publishes_warning_clean_public_doxygen_api():
  root = Path(__file__).resolve().parents[1]
  workflow = (root / ".github" / "workflows" / "pages.yml").read_text()
  cmake = (root / "CMakeLists.txt").read_text()
  mkdocs = (root / "mkdocs.yml").read_text()

  configure = "cmake -S . -B build/docs-api"
  build = "cmake --build build/docs-api --target tess_docs"
  publish = "cp -R build/docs-api/docs/html build/site/api"
  link_check = "python3 tools/check_docs_links.py build/site"

  assert "DOXYGEN_VERSION: 1.17.0" in workflow
  assert (
    "75419ef4f446fc1c24ef12514b574e66"
    "e898ee6f527c6ae2ad84f91a905823c2" in workflow
  )
  assert "sha256sum --check --strict" in workflow
  assert "-DTESS_BUILD_DOCS=ON" in workflow
  assert configure in workflow
  assert build in workflow
  assert publish in workflow
  assert workflow.index(configure) < workflow.index(build)
  assert workflow.index(build) < workflow.index(publish)
  assert workflow.index(publish) < workflow.index(link_check)
  assert workflow.index(publish) < workflow.index("Upload Pages artifact")
  assert (
    "--ignore-missing-anchor api/functions_vars.html#index_b" in workflow
  )
  assert (
    "--ignore-missing-anchor api/functions_vars.html#index_m" in workflow
  )
  assert (
    "--ignore-missing-anchor api/functions_vars.html#index_n" in workflow
  )
  assert (
    "--ignore-missing-anchor api/functions_func.html#index_~" in workflow
  )
  assert (
    "--ignore-missing-anchor api/functions_~.html#index_~" in workflow
  )
  assert "set(DOXYGEN_WARN_AS_ERROR FAIL_ON_WARNINGS)" in cmake
  assert "set(DOXYGEN_WARN_IF_UNDOCUMENTED NO)" in cmake
  assert '"tess::detail::*"' in cmake
  assert "API reference: https://tess.owx.dev/api/" in mkdocs


def test_webgpu_smoke_only_adapter_unavailable_is_unsupported():
  root = Path(__file__).resolve().parents[1]
  source = (
    root / "examples" / "webgpu_compute" / "webgpu_compute.cc"
  ).read_text()
  adapter_ready = source.split("void adapter_ready(", 1)[1].split(
    "\n}\n\n}  // namespace", 1
  )[0]
  run_compute = source.split(
    "[[nodiscard]] bool run_compute(", 1
  )[1].split("\n}\n\nvoid device_ready(", 1)[0]
  finish_readback = source.split("void finish_readback(", 1)[1].split(
    "\n}\n\nvoid device_lost(", 1
  )[0]
  device_ready = source.split("void device_ready(", 1)[1].split(
    "\n}\n\nvoid adapter_ready(", 1
  )[0]
  device_lost = source.split("void device_lost(", 1)[1].split(
    "\n}\n\nvoid device_error(", 1
  )[0]
  device_error = source.split("void device_error(", 1)[1].split(
    "\n}\n\n[[nodiscard]] bool run_compute(", 1
  )[0]
  main = source.split("int main() {", 1)[1]

  assert "constexpr int kAdapterUnavailable = -1;" in source
  assert (
    "status == WGPURequestAdapterStatus_Unavailable" in adapter_ready
  )
  assert "g_status = kAdapterUnavailable;" in adapter_ready
  assert (
    "status == WGPURequestAdapterStatus_CallbackCancelled" in adapter_ready
  )
  assert "g_status = kAdapterRequestCancelled;" in adapter_ready
  assert "g_status = kAdapterRequestFailed;" in adapter_ready
  assert "g_status = kNullAdapter;" in adapter_ready
  assert "device_desc.deviceLostCallbackInfo.mode" in adapter_ready
  assert "WGPUCallbackMode_AllowSpontaneous" in adapter_ready
  assert "device_future.id == 0 &&" in adapter_ready
  assert (
    "transition_status(kRequestingDevice, kDeviceRequestFailed)"
    in adapter_ready
  )
  assert "kAdapterUnavailable" not in device_ready
  assert (
    "status == WGPURequestDeviceStatus_CallbackCancelled" in device_ready
  )
  assert (
    "transition_status(kRequestingDevice, kDeviceRequestCancelled)"
    in device_ready
  )
  assert (
    "transition_status(kRequestingDevice, kDeviceRequestFailed)"
    in device_ready
  )
  assert "transition_status(kRequestingDevice, kNullDevice)" in device_ready
  assert device_ready.index("release_instance();") < (
    device_ready.index("status != WGPURequestDeviceStatus_Success")
  )
  assert "prior_status == kDeviceLost" in device_ready
  assert "prior_status == kDeviceError" in device_ready
  assert "compare_exchange_strong(requesting, kRunningCompute" in device_ready
  assert (
    "status == kPending || status >= kRequestingDevice"
    in device_lost
  )
  assert "backend->notify_device_error();" in device_error
  assert "type == WGPUErrorType_NoError" in device_error
  assert "compare_exchange_weak(status, kDeviceError" in device_error
  assert "device_desc.uncapturedErrorCallbackInfo.callback" in adapter_ready
  assert "device_error" in adapter_ready
  assert "initial_status == kDeviceLost" in run_compute
  assert "initial_status == kDeviceError" in run_compute
  assert "g_backend->notify_device_lost();" in run_compute
  assert "g_backend->notify_device_error();" in run_compute
  assert "kAwaitingReadback" not in device_ready
  assert (
    "g_status.load(std::memory_order_acquire) != kAwaitingReadback"
    in finish_readback
  )
  assert finish_readback.index("!= kAwaitingReadback") < (
    finish_readback.index("kReadbackVerificationFailed")
  )
  assert run_compute.index(
    "compare_exchange_strong(running, kAwaitingReadback"
  ) < (
    run_compute.index("g_backend->readback(")
  )
  assert "transition_status(kPending, kInstanceCreationFailed)" in main
  assert "adapter_future.id == 0 &&" in main
  assert (
    "transition_status(kPending, kAdapterRequestFailed)" in main
  )
  assert "Keep our last reference until device_ready" in adapter_ready


def test_webgpu_pages_smoke_requires_swiftshader_compute_completion():
  root = Path(__file__).resolve().parents[1]
  app = (
    root / "examples" / "webgpu_compute" / "site" / "app.js"
  ).read_text()
  workflow = (root / ".github" / "workflows" / "pages.yml").read_text()

  unsupported = app.split("result === -1", 1)[1].split(
    "result < -1", 1
  )[0]
  timeout = app.split(
    "performance.now() - started > verificationTimeoutMs", 1
  )[1].split("} else {", 1)[0]
  assert re.search(
    r"""dataset\.tessWebgpu = ["']unsupported["']""", unsupported
  )
  assert re.search(r"""dataset\.tessWebgpu = ["']failed["']""", timeout)
  assert not re.search(
    r"""dataset\.tessWebgpu = ["']unsupported["']""", timeout
  )
  assert "stage ${result}" in timeout
  webgpu_smoke = workflow.split(
    "grep -q '>Colony running<'", 1
  )[1].split("- name: Configure Pages", 1)[0]
  assert "--disable-gpu" not in webgpu_smoke
  assert "--virtual-time-budget" not in webgpu_smoke
  assert "Chromium's webgpu-swiftshader test configuration" in webgpu_smoke
  for flag in (
    "--enable-unsafe-webgpu",
    "--use-webgpu-adapter=swiftshader",
    "--enable-dawn-features=allow_unsafe_apis",
    "--disable-dawn-features=use_dxc",
    "--enable-webgpu-developer-features",
    "--use-gpu-in-tests",
    "--enable-accelerated-2d-canvas",
  ):
    assert flag in webgpu_smoke
  assert "python3 tools/wait_for_browser_state.py" in webgpu_smoke
  assert "--dataset tessWebgpu" in webgpu_smoke
  assert "--expected ready" in webgpu_smoke
  assert "--timeout 30" in webgpu_smoke
  assert "const verificationTimeoutMs = 20000;" in app
  assert 'data-tess-webgpu="(ready|unsupported)"' not in workflow


def test_browser_state_websocket_client_frames_are_masked():
  payload = b'{"id":1}'
  mask = b"\x11\x22\x33\x44"

  frame = wait_for_browser_state.encode_client_text_frame(payload, mask)

  assert frame[:2] == b"\x81\x88"
  assert frame[2:6] == mask
  assert bytes(
    byte ^ mask[index % len(mask)]
    for index, byte in enumerate(frame[6:])
  ) == payload


def test_browser_state_rejects_oversized_fragmented_message(monkeypatch):
  class FragmentedMessage:
    def __init__(self):
      self.data = bytearray(b"\x01\x03abc\x80\x03def")

    def settimeout(self, _timeout):
      pass

    def recv(self, size):
      result = bytes(self.data[:size])
      del self.data[:size]
      return result

  monkeypatch.setattr(wait_for_browser_state, "MAX_WEBSOCKET_FRAME_BYTES", 5)
  connection = wait_for_browser_state.DevToolsConnection(FragmentedMessage())

  with pytest.raises(RuntimeError, match="oversized.*message"):
    connection._read_text(deadline=time.monotonic() + 10.0)


def test_browser_state_command_honors_shared_deadline(monkeypatch):
  class EventStream:
    def __init__(self):
      self.data = bytearray(b"\x81\x02{}\x81\x02{}")
      self.timeouts = []

    def sendall(self, _payload):
      pass

    def settimeout(self, timeout):
      self.timeouts.append(timeout)

    def recv(self, size):
      result = bytes(self.data[:size])
      del self.data[:size]
      return result

  times = iter((10.0, 10.25, 11.0))
  monkeypatch.setattr(time, "monotonic", lambda: next(times))
  stream = EventStream()
  connection = wait_for_browser_state.DevToolsConnection(stream)

  with pytest.raises(RuntimeError, match="deadline"):
    connection.command("Runtime.evaluate", {}, deadline=11.0)
  assert stream.timeouts == [1.0, 0.75]


def test_browser_state_partial_frame_reads_share_absolute_deadline(
  monkeypatch,
):
  class DripStream:
    def __init__(self):
      self.timeouts = []

    def settimeout(self, timeout):
      self.timeouts.append(timeout)

    def recv(self, _size):
      now[0] += 0.5
      return b"x"

  now = [20.0]
  monkeypatch.setattr(time, "monotonic", lambda: now[0])
  stream = DripStream()

  with pytest.raises(RuntimeError, match="deadline"):
    wait_for_browser_state._recv_exact(stream, 3, deadline=21.0)
  assert stream.timeouts == [1.0, 0.5]


def test_browser_state_connect_shares_deadline_with_handshake(monkeypatch):
  response = bytearray(b"HTTP/1.1 101 Switching Protocols\r\n\r\n")
  observed = {}

  class HandshakeStream:
    def settimeout(self, timeout):
      observed.setdefault("stream_timeouts", []).append(timeout)

    def sendall(self, _payload):
      pass

  def create_connection(address, timeout):
    observed["address"] = address
    observed["timeout"] = timeout
    return HandshakeStream()

  def recv_exact(_stream, size, deadline):
    observed.setdefault("deadlines", []).append(deadline)
    result = bytes(response[:size])
    del response[:size]
    return result

  monkeypatch.setattr(socket, "create_connection", create_connection)
  monkeypatch.setattr(wait_for_browser_state, "_recv_exact", recv_exact)
  monkeypatch.setattr(time, "monotonic", lambda: 30.0)
  monkeypatch.setattr(
    wait_for_browser_state.hashlib,
    "sha1",
    lambda *_args, **_kwargs: type(
      "Digest", (), {"digest": lambda self: b"\0" * 20}
    )(),
  )
  expected = base64.b64encode(b"\0" * 20).decode("ascii")
  response[-2:-2] = f"Sec-WebSocket-Accept: {expected}\r\n".encode("ascii")

  connection = wait_for_browser_state.DevToolsConnection.connect(
    "ws://127.0.0.1:9222/devtools/page/1", deadline=31.25
  )

  assert observed["address"] == ("127.0.0.1", 9222)
  assert observed["timeout"] == 1.25
  assert observed["stream_timeouts"] == [1.25]
  assert observed["deadlines"] and set(observed["deadlines"]) == {31.25}
  assert isinstance(connection.stream, HandshakeStream)


def test_browser_state_page_wait_reports_browser_exit():
  class ExitedProcess:
    returncode = 17

    def poll(self):
      return self.returncode

  with pytest.raises(RuntimeError, match="browser exited with status 17"):
    wait_for_browser_state._wait_for_page(
      ExitedProcess(), 9222, "http://localhost/demo", time.monotonic() + 1.0
    )


def test_browser_state_dataset_expression_rejects_code_injection():
  assert wait_for_browser_state.dataset_expression("tessWebgpu") == (
    "document.documentElement?.dataset.tessWebgpu || ''"
  )

  with pytest.raises(ValueError):
    wait_for_browser_state.dataset_expression("x;alert(1)")

  assert wait_for_browser_state.page_url_key(
    "HTTP://LOCALHOST:80/demo?mode=ci"
  ) == wait_for_browser_state.page_url_key(
    "http://localhost/demo?mode=ci"
  )
  assert wait_for_browser_state.MAX_WEBSOCKET_FRAME_BYTES == 16 * 1024 * 1024


def test_workflows_use_only_github_owned_sha_pinned_actions():
  root = Path(__file__).resolve().parents[1]
  action_re = re.compile(r"^\s*uses:\s+([^\s@]+)@([^\s#]+)", re.MULTILINE)

  for workflow_path in sorted((root / ".github" / "workflows").glob("*.yml")):
    workflow = workflow_path.read_text(encoding="utf-8")
    actions = action_re.findall(workflow)
    assert actions, f"{workflow_path.name} has no actions"
    for action, revision in actions:
      assert action.startswith("actions/"), (
        f"{workflow_path.name} uses non-GitHub action {action}"
      )
      assert re.fullmatch(r"[0-9a-f]{40}", revision), (
        f"{workflow_path.name} does not SHA-pin {action}"
      )


SHA_A = "a" * 40
SHA_B = "b" * 40
ZEROS = "0" * 40


def test_parse_push_refs_parses_update_and_delete_lines():
  text = (
    f"refs/heads/main {SHA_A} refs/heads/main {SHA_B}\n"
    f"refs/heads/gone {ZEROS} refs/heads/gone {SHA_B}\n"
    f"refs/heads/new {SHA_A} refs/heads/new {ZEROS}\n"
  )
  refs = git_hooks.parse_push_refs(text)
  assert len(refs) == 3
  update, delete, new = refs
  assert update.local_ref == "refs/heads/main"
  assert update.local_sha == SHA_A
  assert update.remote_ref == "refs/heads/main"
  assert update.remote_sha == SHA_B
  assert not update.is_delete() and not update.is_new()
  assert delete.is_delete() and not delete.is_new()
  assert new.is_new() and not new.is_delete()


def test_parse_push_refs_ignores_blank_and_malformed_lines():
  text = f"\nnot a ref line\n{SHA_A}\nrefs/x {SHA_A} refs/x\n"
  assert git_hooks.parse_push_refs(text) == []
  assert git_hooks.parse_push_refs("") == []


def test_pre_push_new_ref_runs_full_suite(monkeypatch):
  # An unresolvable range fails open to the full suite (no bench
  # build: the PR bench-smoke job owns compile rot).
  ref = git_hooks.PushRef("refs/heads/new", SHA_A, "refs/heads/new", ZEROS)
  commands: list[list[str]] = []

  def fake_run(argv, **_kwargs):
    commands.append(argv)
    stdout = f"{SHA_A}\n" if argv == ["git", "rev-parse", "HEAD"] else ""
    return subprocess.CompletedProcess(argv, 0, stdout=stdout)

  monkeypatch.delenv("TESS_PREPUSH_FULL", raising=False)
  monkeypatch.setattr(git_hooks, "read_push_refs", lambda: [ref])
  monkeypatch.setattr(git_hooks, "run", fake_run)

  assert git_hooks.pre_push() == 0
  assert ["ctest", "--preset", "dev"] in commands
  assert all("bench" not in command for command in commands)
  assert ["tools/install_smoke.sh"] not in commands


def test_pre_push_selects_affected_labels(monkeypatch):
  ref = git_hooks.PushRef("refs/heads/x", SHA_A, "refs/heads/x", SHA_B)
  commands: list[list[str]] = []

  def fake_run(argv, **_kwargs):
    commands.append(argv)
    stdout = f"{SHA_A}\n" if argv == ["git", "rev-parse", "HEAD"] else ""
    return subprocess.CompletedProcess(argv, 0, stdout=stdout)

  monkeypatch.delenv("TESS_PREPUSH_FULL", raising=False)
  monkeypatch.setattr(git_hooks, "read_push_refs", lambda: [ref])
  monkeypatch.setattr(
    git_hooks, "push_range_paths",
    lambda _updates: ["include/tess/path/astar.h"],
  )
  monkeypatch.setattr(git_hooks, "run", fake_run)

  assert git_hooks.pre_push() == 0
  selected = [c for c in commands if c[:2] == ["ctest", "--preset"]]
  assert selected == [
    [
      "ctest", "--preset", "dev", "-L",
      "(^|;)(prepush:always|subsystem:path)(;|$)",
    ]
  ]


def test_pre_push_docs_only_builds_without_tests(monkeypatch):
  ref = git_hooks.PushRef("refs/heads/x", SHA_A, "refs/heads/x", SHA_B)
  commands: list[list[str]] = []

  def fake_run(argv, **_kwargs):
    commands.append(argv)
    stdout = f"{SHA_A}\n" if argv == ["git", "rev-parse", "HEAD"] else ""
    return subprocess.CompletedProcess(argv, 0, stdout=stdout)

  monkeypatch.delenv("TESS_PREPUSH_FULL", raising=False)
  monkeypatch.setattr(git_hooks, "read_push_refs", lambda: [ref])
  monkeypatch.setattr(
    git_hooks, "push_range_paths",
    lambda _updates: ["docs/planning/notes.md", "README.md"],
  )
  monkeypatch.setattr(git_hooks, "run", fake_run)

  assert git_hooks.pre_push() == 0
  assert not any(c[0] == "ctest" for c in commands)
  assert ["cmake", "--build", "--preset", "dev"] in commands


def test_pre_push_full_opt_in_overrides_everything(monkeypatch):
  ref = git_hooks.PushRef("refs/heads/x", SHA_A, "refs/heads/x", SHA_B)
  commands: list[list[str]] = []

  def fake_run(argv, **_kwargs):
    commands.append(argv)
    stdout = f"{SHA_A}\n" if argv == ["git", "rev-parse", "HEAD"] else ""
    return subprocess.CompletedProcess(argv, 0, stdout=stdout)

  monkeypatch.setenv("TESS_PREPUSH_FULL", "1")
  monkeypatch.setattr(git_hooks, "read_push_refs", lambda: [ref])
  monkeypatch.setattr(
    git_hooks, "push_range_paths",
    lambda _updates: ["docs/planning/notes.md"],
  )
  monkeypatch.setattr(git_hooks, "run", fake_run)

  assert git_hooks.pre_push() == 0
  assert ["ctest", "--preset", "dev"] in commands
  assert ["tools/install_smoke.sh"] in commands
  assert ["tools/fetchcontent_smoke.sh"] in commands


def _classify(names):
  return git_hooks.classify_push_paths(
    names, frozenset({"tess_path_test", "tess_storage_test"})
  )


def test_classify_subsystem_paths_select_labels():
  verdict, labels = _classify(["include/tess/path/astar.h"])
  assert verdict == "select"
  assert labels == frozenset({"subsystem:path"})


def test_classify_core_and_umbrellas_run_full():
  for name in (
    "include/tess/core/lattice.h",
    "include/tess/storage/world.h",
    "include/tess/tess.h",
    "include/tess/pathfinding.h",
    "include/tess/version.h.in",
  ):
    assert _classify([name])[0] == "full", name


def test_classify_test_source_selects_its_target():
  verdict, labels = _classify(["tests/tess_path_test.cc"])
  assert verdict == "select"
  assert labels == frozenset({"target:tess_path_test"})


def test_classify_unknown_test_source_runs_full():
  assert _classify(["tests/allocation_counter.cc"])[0] == "full"
  assert _classify(["tests/CMakeLists.txt"])[0] == "full"


def test_classify_tools_and_cmake_run_full():
  assert _classify(["tools/git_hooks.py"])[0] == "full"
  assert _classify(["CMakeLists.txt"])[0] == "full"
  assert _classify(["cmake/warnings.cmake"])[0] == "full"


def test_classify_bench_and_examples_build_only():
  verdict, labels = _classify(["bench/tess_bench.cc", "examples/demo.cc"])
  assert verdict == "build-only"
  assert labels == frozenset()


def test_classify_alloc_hooks_source_runs_full():
  # Compiled into dev test targets, not only benchmarks.
  assert _classify(["bench/tess_diagnostics_alloc_hooks.cc"])[0] == "full"


def test_classify_docs_only_is_build_only():
  verdict, _labels = _classify(["docs/planning/x.md", "LICENSE"])
  assert verdict == "build-only"


def test_classify_mixed_inert_and_subsystem_selects():
  verdict, labels = _classify(
    ["docs/x.md", "include/tess/query/span.h"]
  )
  assert verdict == "select"
  assert labels == frozenset({"subsystem:query"})


def test_selection_regex_is_delimiter_anchored_and_always_included():
  regex = git_hooks.selection_regex(frozenset({"subsystem:query"}))
  assert regex == "(^|;)(prepush:always|subsystem:query)(;|$)"
  # Matches both separate and ;-joined label forms, without
  # substring collisions.
  pattern = re.compile(regex)
  assert pattern.search("subsystem:query")
  assert pattern.search("target:tess_x;subsystem:query;subsystem:core")
  assert not pattern.search("subsystem:querytail")
  assert not pattern.search("target:subsystem:query_test")


def test_gtest_targets_parses_real_cmake():
  targets = git_hooks.gtest_targets()
  assert "tess_path_test" in targets
  assert "tess_counter_golden_probe" not in targets


CMAKE_FIXTURE = """
add_executable(tess_alpha_test alpha.cc)
gtest_discover_tests(tess_alpha_test)

add_executable(
  tess_beta_test
  helper.cc
  beta.cc
)
target_link_libraries(tess_beta_test PRIVATE tess::tess)

add_executable(other_tool tool.cc)

add_test(
  NAME tess_package_selection
  COMMAND cmake -P test-package.cmake
)
"""

FRAGMENTS_FIXTURE = {
  "tess_alpha_test.md": "# tess_alpha_test\n\n- `tess_alpha_test`: covers alpha.\n",
  "tess_beta_test.md": "# tess_beta_test\n\n- `tess_beta_test`: covers beta.\n",
  "tess_package_selection.md": (
    "# tess_package_selection\n\n- `tess_package_selection`: covers packaging.\n"
  ),
  "test_tool.py.md": "# test_tool.py\n\n- `tests/test_tool.py`: covers a tool.\n",
}
PYTEST_FIXTURE = ["test_tool.py"]


def test_extract_cmake_test_targets_handles_multiline_forms():
  targets = git_hooks.extract_cmake_test_targets(CMAKE_FIXTURE)
  assert targets == [
    "tess_alpha_test",
    "tess_beta_test",
    "tess_package_selection",
  ]


def test_agents_fragment_issues_empty_when_synchronized():
  issues = git_hooks.agents_fragment_issues(
    CMAKE_FIXTURE, PYTEST_FIXTURE, FRAGMENTS_FIXTURE
  )
  assert issues == []


def test_agents_fragment_issues_reports_missing_target_fragment():
  fragments = dict(FRAGMENTS_FIXTURE)
  del fragments["tess_beta_test.md"]
  issues = git_hooks.agents_fragment_issues(
    CMAKE_FIXTURE, PYTEST_FIXTURE, fragments
  )
  assert issues == [
    "tests/agents.d/tess_beta_test.md: missing fragment for tess_beta_test"
  ]


def test_agents_fragment_issues_reports_missing_pytest_fragment():
  fragments = dict(FRAGMENTS_FIXTURE)
  del fragments["test_tool.py.md"]
  issues = git_hooks.agents_fragment_issues(
    CMAKE_FIXTURE, PYTEST_FIXTURE, fragments
  )
  assert issues == [
    "tests/agents.d/test_tool.py.md: missing fragment for test_tool.py"
  ]


def test_agents_fragment_issues_reports_orphan_fragment():
  fragments = dict(FRAGMENTS_FIXTURE)
  fragments["tess_gone_test.md"] = "# tess_gone_test\n\n- gone.\n"
  issues = git_hooks.agents_fragment_issues(
    CMAKE_FIXTURE, PYTEST_FIXTURE, fragments
  )
  assert issues == [
    "tests/agents.d/tess_gone_test.md: no matching test target or"
    " tests/test_*.py file"
  ]


def test_agents_fragment_issues_reports_empty_fragment_body():
  fragments = dict(FRAGMENTS_FIXTURE)
  fragments["tess_beta_test.md"] = "# tess_beta_test\n\n   \n"
  issues = git_hooks.agents_fragment_issues(
    CMAKE_FIXTURE, PYTEST_FIXTURE, fragments
  )
  assert issues == [
    "tests/agents.d/tess_beta_test.md: fragment body is empty"
  ]


def test_agents_fragment_issues_reports_heading_mismatch():
  fragments = dict(FRAGMENTS_FIXTURE)
  fragments["tess_beta_test.md"] = (
    "# tess_alpha_test\n\n- `tess_beta_test`: covers beta.\n"
  )
  issues = git_hooks.agents_fragment_issues(
    CMAKE_FIXTURE, PYTEST_FIXTURE, fragments
  )
  assert issues == [
    "tests/agents.d/tess_beta_test.md: first line must be"
    " '# tess_beta_test'"
  ]


def test_agents_fragment_issues_rejects_trailing_heading_whitespace():
  fragments = dict(FRAGMENTS_FIXTURE)
  fragments["tess_beta_test.md"] = (
    "# tess_beta_test \n\n- `tess_beta_test`: covers beta.\n"
  )
  issues = git_hooks.agents_fragment_issues(
    CMAKE_FIXTURE, PYTEST_FIXTURE, fragments
  )
  assert issues == [
    "tests/agents.d/tess_beta_test.md: first line must be"
    " '# tess_beta_test'"
  ]


def test_ci_hook_unit_tests_enumerate_every_pytest_file():
  """The CI pytest invocation lists files explicitly, so a new suite that
  is not added there silently never runs (it has happened four times).
  Pin the enumeration to the tests/test_*.py glob, both directions."""
  root = Path(__file__).resolve().parents[1]
  workflow = (root / ".github" / "workflows" / "ci.yml").read_text()
  lines = workflow.splitlines()
  try:
    start = lines.index("          pytest")
  except ValueError:
    raise AssertionError(
      "ci.yml no longer contains the expected 'pytest' invocation line"
    ) from None
  enumerated = set()
  for line in lines[start + 1 :]:
    stripped = line.strip()
    if not stripped.startswith("tests/test_") or not stripped.endswith(
      ".py"
    ):
      break
    enumerated.add(stripped.removeprefix("tests/"))
  present = {path.name for path in (root / "tests").glob("test_*.py")}
  assert enumerated == present, (
    f"missing from ci.yml pytest invocation: {sorted(present - enumerated)}; "
    f"listed but absent on disk: {sorted(enumerated - present)}"
  )


def test_repo_agents_fragments_match_targets_and_pytest_files():
  root = Path(__file__).resolve().parents[1]
  fragments_dir = root / "tests" / "agents.d"
  fragments = {
    path.name: path.read_text() for path in fragments_dir.glob("*.md")
  }
  pytest_names = sorted(
    path.name for path in (root / "tests").glob("test_*.py")
  )
  issues = git_hooks.agents_fragment_issues(
    (root / "CMakeLists.txt").read_text()
    + (root / "tests" / "CMakeLists.txt").read_text(),
    pytest_names,
    fragments,
  )
  assert issues == []


def test_compiled_dev_requirements_include_exact_direct_pins():
  root = Path(__file__).resolve().parents[1]

  def requirement_blocks(path: Path) -> list[list[str]]:
    blocks: list[list[str]] = []
    current: list[str] | None = None
    for line in path.read_text().splitlines():
      is_requirement = (
        bool(line) and not line[0].isspace() and not line.startswith("#")
      )
      if is_requirement:
        current = [line]
        blocks.append(current)
      elif current is not None:
        current.append(line)
    return blocks

  def requirement_starts(path: Path) -> list[str]:
    return [
      block[0].removesuffix("\\").rstrip() for block in requirement_blocks(path)
    ]

  direct = requirement_starts(root / "requirements-dev.in")
  lock_path = root / "requirements-dev.txt"
  compiled = requirement_starts(lock_path)

  assert set(direct) <= set(compiled)
  for requirement in compiled:
    package_and_version = requirement.split(";", 1)[0].strip()
    assert package_and_version.count("==") == 1
    package, version = package_and_version.split("==", 1)
    assert package and version
  for block in requirement_blocks(lock_path):
    hashes = [
      line.strip().removesuffix("\\").rstrip()
      for line in block[1:]
      if line.lstrip().startswith("--hash=sha256:")
    ]
    assert hashes
    for item in hashes:
      digest = item.removeprefix("--hash=sha256:")
      assert len(digest) == 64
      assert not set(digest) - set("0123456789abcdef")


def test_compiled_dev_requirements_keep_supported_environment_dependencies():
  root = Path(__file__).resolve().parents[1]
  requirement_lines = {
    line.split("==", 1)[0]: line
    for line in (root / "requirements-dev.txt").read_text().splitlines()
    if line and not line[0].isspace() and not line.startswith("#")
  }

  assert "sys_platform == 'win32'" in requirement_lines["colorama"]
  for package in ("exceptiongroup", "tomli", "typing-extensions"):
    assert "python_full_version < '3.11'" in requirement_lines[package]


def test_documented_clang_format_version_matches_direct_pin():
  root = Path(__file__).resolve().parents[1]
  direct = (root / "requirements-dev.in").read_text()
  match = re.search(r"^clang-format==([^\s]+)$", direct, re.MULTILINE)

  assert match is not None
  dependencies = (root / "docs" / "dependencies.md").read_text()
  assert (
    f"clang-format Python package version: `{match.group(1)}`" in dependencies
  )


def test_compiled_dev_requirements_records_reproducible_command():
  root = Path(__file__).resolve().parents[1]
  header = (root / "requirements-dev.txt").read_text().splitlines()[:2]

  assert header == [
    "# This file was autogenerated by uv via the following command:",
    "#    tools/compile_requirements.sh",
  ]


def test_compile_requirements_uses_pinned_uv_and_canonical_command(tmp_path):
  root = Path(__file__).resolve().parents[1]
  wrapper = root / "tools" / "compile_requirements.sh"
  fake_uv = tmp_path / "uv"
  args_log = tmp_path / "uv-args"
  output = tmp_path / "requirements-dev.txt"
  fake_uv.write_text(
    "#!/bin/sh\n"
    "set -eu\n"
    "if [ \"$1\" = --version ]; then\n"
    "  printf 'uv 0.11.28\\n'\n"
    "  exit 0\n"
    "fi\n"
    "printf '%s\\n' \"$@\" > \"$UV_ARGS_LOG\"\n"
  )
  fake_uv.chmod(0o755)
  env = os.environ.copy()
  env["PATH"] = f"{tmp_path}:{env['PATH']}"
  env["UV_ARGS_LOG"] = str(args_log)

  subprocess.run(
    [wrapper, output],
    cwd=root,
    env=env,
    check=True,
    text=True,
    capture_output=True,
  )

  assert args_log.read_text().splitlines() == [
    "pip",
    "compile",
    "--universal",
    "--python-version",
    "3.10",
    "--upgrade",
    "--generate-hashes",
    "--exclude-newer",
    "2026-07-13T00:00:00Z",
    "--custom-compile-command",
    "tools/compile_requirements.sh",
    "requirements-dev.in",
    "-o",
    str(output),
  ]


LABELED_CALL_RE = re.compile(
  r"tess_discover_tests\(\s*(\w+)([^)]*)\)", re.MULTILINE
)


def _declared_labels():
  text = (
    git_hooks.REPO_ROOT / "tests" / "CMakeLists.txt"
  ).read_text(encoding="utf-8")
  calls = {}
  for match in LABELED_CALL_RE.finditer(text):
    target, args = match.group(1), match.group(2).split()
    labels = set()
    if "ALWAYS" in args:
      labels.add("prepush:always")
    if "LABELS" in args:
      labels |= {
        f"subsystem:{token}"
        for token in args[args.index("LABELS") + 1:]
      }
    calls[target] = labels
  return calls


def test_every_discovered_target_declares_its_impact():
  # The tested source-to-test mapping (redesign section 6): every
  # target declares subsystems or is explicitly ALWAYS/bare.
  calls = _declared_labels()
  assert set(calls) == set(git_hooks.gtest_targets())
  bare_allowed = {"tess_grid_benchmark_data_test"}
  for target, labels in calls.items():
    subsystems = {
      label.split(":", 1)[1]
      for label in labels
      if label.startswith("subsystem:")
    }
    assert subsystems <= git_hooks.SUBSYSTEM_LABELS, (target, subsystems)
    if not labels:
      assert target in bare_allowed, (
        f"{target} declares no labels; add its impact set"
      )


def test_every_subsystem_has_at_least_one_labeled_test():
  # A subsystem whose changes would select zero tests is a silent
  # under-selection bug — no acknowledged gaps: every subsystem
  # (including gpu and debug) has direct tests today.
  covered = set()
  for labels in _declared_labels().values():
    covered |= {
      label.split(":", 1)[1]
      for label in labels
      if label.startswith("subsystem:")
    }
  assert covered == set(git_hooks.SUBSYSTEM_LABELS)


def test_subsystem_vocabulary_matches_include_tree():
  subsystems = {
    child.name
    for child in (git_hooks.REPO_ROOT / "include" / "tess").iterdir()
    if child.is_dir()
  }
  assert subsystems == set(git_hooks.SUBSYSTEM_LABELS)


def test_labels_propagate_into_discovered_tests():
  # Build-level check that CMake actually attached the labels; runs
  # wherever a configured dev build exists (the CI dev job runs it
  # explicitly after building).
  build_dir = git_hooks.REPO_ROOT / "build" / "dev"
  if not (build_dir / "CTestTestfile.cmake").exists():
    pytest.skip("no configured dev build")
  listing = subprocess.run(
    ["ctest", "--test-dir", str(build_dir), "--show-only=json-v1"],
    capture_output=True,
    text=True,
    timeout=600,
  )
  assert listing.returncode == 0
  payload = json.loads(listing.stdout)
  by_label = set()
  path_test_labels = set()
  for test in payload["tests"]:
    props = {
      prop["name"]: prop.get("value")
      for prop in test.get("properties", [])
    }
    labels = set()
    for entry in props.get("LABELS") or []:
      labels |= set(entry.split(";"))
    by_label |= labels
    if "target:tess_path_test" in labels:
      path_test_labels |= labels
  assert "prepush:always" in by_label
  assert {"subsystem:path", "target:tess_path_test"} <= path_test_labels


def test_empty_ref_input_fails_open_to_full():
  assert git_hooks.push_range_paths([]) is None


def test_conditional_target_source_runs_full():
  verdict, _labels = git_hooks.classify_push_paths(
    ["tests/tess_grid_benchmark_data_test.cc"],
    frozenset({"tess_grid_benchmark_data_test"}),
  )
  assert verdict == "full"


def test_workflow_changes_are_not_inert():
  assert _classify([".github/workflows/ci.yml"])[0] == "full"


def _ccache_namespaces() -> dict[str, list[str]]:
  """Every ccache cache namespace declared across the workflows.

  Returns {workflow_path: [namespace, ...]}, where a namespace is the
  cache-key prefix with the per-run discriminator removed. Parsed as
  text because the repository carries no YAML dependency.
  """
  root = Path(__file__).resolve().parents[1]
  presets = _configure_preset_names(root)
  discriminator_re = re.compile(r"\$\{\{ github\.\w+ \}\}$")
  found: dict[str, list[str]] = {}

  for path in sorted((root / ".github" / "workflows").glob("*.yml")):
    text = path.read_text()
    names: list[str] = []
    keys_seen = 0
    for raw_line in text.splitlines():
      line = raw_line.strip()
      if line.startswith("key: ccache-"):
        keys_seen += 1
        namespace = discriminator_re.sub("", line[len("key: ") :])
      elif line.startswith("restore-keys: ccache-"):
        namespace = line[len("restore-keys: ") :]
      elif line.startswith("ccache-"):
        namespace = line  # entry in a block-style restore-keys list
      elif line.startswith("cache_fallback: ccache-"):
        # A matrix-supplied fallback namespace. It reaches restore-keys
        # through an expression, so it would otherwise be invisible here.
        namespace = line[len("cache_fallback: ") :]
      else:
        continue
      # A matrix key is a template. Expand it over every configure preset
      # so the sibling namespaces it really produces -- ccache-dev-asan--,
      # ccache-dev-cppcheck-- and the rest -- are the values compared.
      # Expanding over all presets is a superset of what any matrix can
      # select, so disjointness here implies disjointness in practice.
      if "${{ matrix.preset }}" in namespace:
        names.extend(
          namespace.replace("${{ matrix.preset }}", preset)
          for preset in presets
        )
      else:
        names.append(namespace)

    # Fail closed: a cache step whose key this parser cannot read would
    # otherwise be silently skipped, which is how the first version of
    # this helper missed every matrix key it was written to cover.
    cache_steps = text.count("path: .ccache")
    assert keys_seen == cache_steps, (
      f"{path.name}: parsed {keys_seen} ccache keys but found "
      f"{cache_steps} ccache cache steps"
    )
    if names:
      found[path.name] = names
  return found


def _configure_preset_names(root: Path) -> list[str]:
  presets = json.loads((root / "CMakePresets.json").read_text())
  names = [
    preset["name"]
    for group in ("configurePresets", "buildPresets", "testPresets")
    for preset in presets.get(group, [])
  ]
  assert names
  return sorted(set(names))


def test_no_preset_name_contains_the_namespace_terminator():
  """The premise the terminator relies on.

  `--` separates a ccache namespace from its discriminator. That only
  keeps namespaces disjoint while no preset name can contain `--`.
  """
  root = Path(__file__).resolve().parents[1]
  names = _configure_preset_names(root)

  assert names, "no presets parsed; the premise below would be vacuous"
  offenders = [name for name in names if "--" in name]
  assert offenders == []


def test_every_ccache_namespace_is_terminated():
  """Restore keys match by prefix, so namespaces must not nest.

  `restore-keys: ccache-dev-` also matches `ccache-dev-asan-*` and
  `ccache-dev-cppcheck-*`, so the dev job restored a sanitizer preset's
  objects and rebuilt cold. Terminating every namespace with `--` makes
  the prefix relation impossible, given the preset premise above.
  """
  namespaces = _ccache_namespaces()

  assert namespaces, "no ccache cache steps found; the parser is stale"
  for workflow, names in namespaces.items():
    for name in names:
      assert name.endswith("--"), (workflow, name)


def test_no_ccache_namespace_is_a_prefix_of_another():
  """Checks the outcome directly, not merely the `--` convention.

  The terminator is the mechanism; disjointness is the property it buys,
  and this still holds if a namespace ever adopts another separator.
  """
  namespaces = _ccache_namespaces()
  every = sorted({name for names in namespaces.values() for name in names})

  nested = [
    (outer, inner)
    for outer in every
    for inner in every
    if outer != inner and inner.startswith(outer)
  ]

  assert nested == []


def test_every_ccache_workflow_caps_its_cache_size():
  """Each workflow must cap its own ccache.

  Workflow-level `env` does not cross workflow boundaries, so the cap in
  ci.yml leaves the schedule-only advisory build and the dispatch-only
  paired build on ccache's 5 GiB default -- against a 10 GB repository
  quota that every run already evicts into.
  """
  root = Path(__file__).resolve().parents[1]
  uncapped = []
  for path in sorted((root / ".github" / "workflows").glob("*.yml")):
    text = path.read_text()
    if "path: .ccache" not in text:
      continue
    if "CCACHE_MAXSIZE:" not in text:
      uncapped.append(path.name)

  assert uncapped == []


def test_every_workflow_job_declares_a_timeout():
  """A job without one inherits GitHub's 360-minute default.

  That default is charged at the runner's multiplier -- two on Windows,
  ten on macOS -- so a single hung job is expensive. Checking the class
  rather than the jobs that happened to lack one at the time.
  """
  root = Path(__file__).resolve().parents[1]
  missing = []
  for path in sorted((root / ".github" / "workflows").glob("*.yml")):
    body = path.read_text().split("\njobs:\n", 1)[1]
    # Split on top-level job keys and check each block for a timeout.
    blocks = re.split(r"^  ([a-z0-9][a-z0-9_-]*):$", body, flags=re.M)
    for name, block in zip(blocks[1::2], blocks[2::2]):
      if "timeout-minutes:" not in block:
        missing.append(f"{path.name}:{name}")

  assert missing == []


def test_cache_fallbacks_are_compiler_matched():
  """A fallback from a different compiler cannot hit.

  ccache keys on compiler identity, so borrowing the Clang `quality`
  cache for the GCC exception-free cell would download a large
  irrelevant cache and re-save it -- strictly worse than no fallback.
  Each cell's fallback must name a namespace built by its own compiler.
  """
  root = Path(__file__).resolve().parents[1]
  workflow = (root / ".github" / "workflows" / "ci.yml").read_text()
  block = workflow.split("  no-exceptions:\n", 1)[1].split("\n    env:", 1)[0]

  cells = re.findall(
    r"- name: [^\n]*\n\s+cc: (\S+)\n(?:\s+\S+: [^\n]*\n)*?"
    r"\s+cache_fallback: (\S+)",
    block,
  )

  assert len(cells) == 2, cells
  # ccache-gcc-- is written by the gcc job (CC=gcc); every other namespace
  # in this workflow is written by a Clang job.
  for compiler, fallback in cells:
    if compiler == "gcc":
      assert fallback == "ccache-gcc--", (compiler, fallback)
    else:
      assert fallback != "ccache-gcc--", (compiler, fallback)
