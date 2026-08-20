"""CI reliability and workflow-velocity contract tests."""

from __future__ import annotations

import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

import pytest


def _job_body(workflow: str, job_id: str) -> str:
  body = workflow.split(f"  {job_id}:\n", 1)[1]
  following = re.search(r"^  [a-z0-9][a-z0-9_-]*:$", body, flags=re.M)
  return body[: following.start()] if following else body


def _workflow_run_source(source: str) -> str:
  lines = source.splitlines()
  assert all(not line or line.startswith("          ") for line in lines)
  return "\n".join(line.removeprefix("          ") for line in lines)


def _recovery_block(workflow: str, job_id: str) -> str:
  job = _job_body(workflow, job_id)
  start = "# CI_RECOVERY_BLOCK_BEGIN\n"
  end = "# CI_RECOVERY_BLOCK_END"
  assert job.count(start) == 1
  assert job.count(end) == 1
  return _workflow_run_source(job.split(start, 1)[1].split(end, 1)[0])


def test_generic_example_smokes_use_cheap_traffic_checks():
  root = Path(__file__).resolve().parents[1]
  workflow = (root / ".github" / "workflows" / "ci.yml").read_text()
  traffic_targets = {
    "tess_web_traffic_model",
    "tess_web_traffic_diagnostics_model",
  }

  for job_id, step_name in (
    ("dev", "Example smoke"),
    ("no-exceptions", "Run exception-free examples"),
  ):
    job = _job_body(workflow, job_id)
    smoke = job.split(f"      - name: {step_name}\n", 1)[1].split(
      "\n      - name:", 1
    )[0]
    assert "--self-check-smoke" in smoke
    assert all(target in smoke for target in traffic_targets)
    assert "case \"$(basename \"$example\")\"" in smoke

  no_exceptions = _job_body(workflow, "no-exceptions")
  smoke = no_exceptions.split(
    "      - name: Run exception-free examples\n", 1
  )[1].split("\n      - name:", 1)[0]
  examples = (root / "examples" / "CMakeLists.txt").read_text()
  declared = re.findall(
    r"^\s*add_executable\(\s*(tess_[A-Za-z0-9_]+)(?=\s|\))",
    examples,
    flags=re.M,
  )
  optional = set()
  for option in ("TESS_ENABLE_ENTT", "TESS_ENABLE_FLECS"):
    block = examples.split(f"if({option})", 1)[1].split("endif()", 1)[0]
    optional.update(
      re.findall(r"add_executable\((tess_[A-Za-z0-9_]+)", block)
    )
  expected = re.findall(r'test \"\$ran\" -eq ([0-9]+)', smoke)
  assert optional == {"tess_entt_pawns", "tess_flecs_pawns"}
  assert expected == [str(len(declared) - len(optional))]


def _ci_recovery_issue(
  issue: dict[str, object],
  timeline: list[dict[str, object]],
  run_id: int = 1234,
  run_attempt: int = 2,
) -> str:
  """Execute the reporter's exact recovery-ownership classifier."""
  root = Path(__file__).resolve().parents[1]
  workflow = (root / ".github/workflows/ci.yml").read_text()
  reporter = _job_body(workflow, "report-failure")
  start = "# CI_RECOVERY_CLASSIFIER_BEGIN\n"
  end = "# CI_RECOVERY_CLASSIFIER_END"
  assert start in reporter
  source = _workflow_run_source(
    reporter.split(start, 1)[1].split(end, 1)[0]
  )
  result = subprocess.run(
    (sys.executable, "-c", source),
    input=json.dumps({"issue": issue, "timeline": timeline}),
    check=True,
    capture_output=True,
    text=True,
    env={
      **os.environ,
      "SOURCE_RUN_ID": str(run_id),
      "SOURCE_RUN_ATTEMPT": str(run_attempt),
    },
  )
  return result.stdout.strip()


def _bot_report(
  *,
  run_id: int = 1234,
  attempt: int = 1,
  created_at: str = "2026-08-19T12:00:00Z",
) -> dict[str, object]:
  return {
    "event": "commented",
    "body": (
      f"<!-- tess-ci-report run_id={run_id} attempt={attempt} -->\n"
      "CI push run attempt 1 failed."
    ),
    "created_at": created_at,
    "updated_at": created_at,
    "user": {"login": "github-actions[bot]"},
  }


def test_ci_failure_reporter_reconciles_only_owned_same_run_issue():
  report = _bot_report()
  issue = {
    "number": 218,
    "state": "OPEN",
    "updatedAt": report["updated_at"],
  }

  assert _ci_recovery_issue(issue, [report]) == "218"
  assert _ci_recovery_issue(issue, [report], run_id=9999) == ""
  assert _ci_recovery_issue(issue, [report], run_attempt=1) == ""


@pytest.mark.parametrize(
  "mutation",
  (
    "human-comment",
    "edited-report",
    "reopened",
    "malformed-marker",
    "same-attempt",
    "body-edit",
  ),
)
def test_ci_failure_reporter_preserves_non_owned_issue_activity(mutation):
  report = _bot_report()
  issue = {
    "number": 218,
    "state": "OPEN",
    "updatedAt": report["updated_at"],
  }
  timeline = [report]
  later = "2026-08-19T12:01:00Z"
  if mutation == "human-comment":
    timeline.append({
      **_bot_report(created_at=later),
      "user": {"login": "maintainer"},
    })
    issue["updatedAt"] = later
  elif mutation == "edited-report":
    report["updated_at"] = later
    issue["updatedAt"] = later
  elif mutation == "reopened":
    timeline.append({"event": "reopened", "created_at": later})
    issue["updatedAt"] = later
  elif mutation == "malformed-marker":
    report["body"] = "CI failed without an ownership marker"
  elif mutation == "same-attempt":
    report["body"] = str(report["body"]).replace("attempt=1", "attempt=2")
  elif mutation == "body-edit":
    issue["updatedAt"] = later

  assert _ci_recovery_issue(issue, timeline) == ""


def test_ci_failure_reporter_recovery_is_fail_closed_and_rechecked():
  root = Path(__file__).resolve().parents[1]
  workflow = (root / ".github/workflows/ci.yml").read_text()
  reporter = _job_body(workflow, "report-failure")
  completion_workflow = (
    root / ".github/workflows/ci-failure-recovery.yml"
  ).read_text()
  completion = _job_body(completion_workflow, "reconcile")

  assert "github.run_attempt > 1" in reporter
  assert "has_unresolved=" in reporter
  assert '.result == "failure" or .result == "cancelled"' in reporter
  assert "<!-- tess-ci-report run_id=${GITHUB_RUN_ID}" in reporter
  for source, expression in (
    ("SOURCE_RUN_ID", "github.run_id"),
    ("SOURCE_RUN_ATTEMPT", "github.run_attempt"),
    ("SOURCE_RUN_URL", "github.server_url"),
  ):
    assert f"{source}:" in reporter
    assert expression in reporter
  assert reporter.count("CI_RECOVERY_CLASSIFIER_BEGIN") == 1
  assert completion.count("CI_RECOVERY_CLASSIFIER_BEGIN") == 1
  assert _recovery_block(workflow, "report-failure") == _recovery_block(
    completion_workflow, "reconcile"
  )

  for recovery in (reporter, completion):
    assert '--json number,state,updatedAt' in recovery
    assert '[ "$issue_count" -eq 1 ] || return 0' in recovery
    assert recovery.count("/timeline?per_page=100") == 1
    assert "owned_issue=$(read_owned_issue)" in recovery
    assert "final_issue=$(read_owned_issue)" in recovery
    assert recovery.index("# Final read") < recovery.index(
      "gh issue close"
    )
    assert "--paginate --slurp" in recovery
    close = recovery.split("gh issue close", 1)[1].split("exit 0", 1)[0]
    assert "||" not in close
    assert "SOURCE_RUN_ATTEMPT" in close
    assert "SOURCE_RUN_URL" in close


def test_ci_completion_recovery_covers_failed_job_only_reruns():
  root = Path(__file__).resolve().parents[1]
  workflows = tuple((root / ".github" / "workflows").glob("*.yml"))
  names = []
  for path in workflows:
    match = re.search(r"^name: (.+)$", path.read_text(), flags=re.M)
    assert match is not None, path
    names.append(match.group(1))
  assert names.count("CI") == 1

  workflow = (root / ".github/workflows/ci.yml").read_text()
  reporter = _job_body(workflow, "report-failure")
  recovery = (
    root / ".github/workflows/ci-failure-recovery.yml"
  ).read_text()
  trigger = recovery.split("on:\n", 1)[1].split("\n\n", 1)[0]
  assert trigger == (
    "  workflow_run:\n"
    "    workflows: [CI]\n"
    "    branches: [main]\n"
    "    types: [completed]"
  )
  permissions = recovery.split("permissions:\n", 1)[1].split("\n\n", 1)[0]
  assert permissions == "  actions: read\n  issues: write"
  jobs = recovery.split("jobs:\n", 1)[1]
  assert re.findall(r"^  ([a-z0-9-]+):$", jobs, flags=re.M) == ["reconcile"]

  completion = _job_body(recovery, "reconcile")
  condition = completion.split("if: >-\n", 1)[1].split("runs-on:", 1)[0]
  assert " ".join(condition.split()) == (
    "${{ github.event.workflow_run.event != 'pull_request' && "
    "github.event.workflow_run.head_repository.full_name == "
    "github.repository && github.event.workflow_run.head_branch == 'main' "
    "&& github.event.workflow_run.run_attempt > 1 && "
    "github.event.workflow_run.conclusion == 'success' }}"
  )
  assert len(re.findall(r"^      - ", completion, flags=re.M)) == 1
  assert completion.count("        run: |\n") == 1
  assert "actions/checkout" not in completion
  assert "actions/download-artifact" not in completion
  assert "actions/cache" not in completion
  assert "uses:" not in completion
  assert "secrets" not in completion
  assert "tools/" not in completion
  assert "GITHUB_WORKSPACE" not in completion
  assert "github.workspace" not in completion

  for source, expression in (
    ("SOURCE_RUN_ID", "github.event.workflow_run.id"),
    ("SOURCE_RUN_ATTEMPT", "github.event.workflow_run.run_attempt"),
    ("SOURCE_RUN_URL", "github.event.workflow_run.html_url"),
  ):
    assert f"{source}: ${{{{ {expression} }}}}" in completion
  script = completion.split("        run: |\n", 1)[1]
  assert "${{" not in script
  assert "GITHUB_RUN_ID" not in script
  assert "GITHUB_RUN_ATTEMPT" not in script

  for job in (reporter, recovery):
    assert "group: ci-failure-report" in job
    assert "queue: max" in job


def test_ci_setup_tools_are_pinned_bounded_and_used_without_apt_ccache():
  root = Path(__file__).resolve().parents[1]
  lock = json.loads((root / "ci" / "tools.lock.json").read_text())
  workflows = tuple((root / ".github" / "workflows").glob("*.yml"))
  workflow_text = "\n".join(path.read_text() for path in workflows)
  installer = (root / "tools" / "install_ci_ccache.sh").read_text()
  apt = (root / "tools" / "ci_apt_install.sh").read_text()
  stats = (root / "tools" / "report_ccache_stats.sh").read_text()

  assert lock["ccache"] == {
    "version": "4.13.6",
    "linux_x86_64": (
      "https://github.com/ccache/ccache/releases/download/v4.13.6/"
      "ccache-4.13.6-linux-x86_64-musl-static.tar.xz"
    ),
    "sha256": (
      "156ec57c5198cc849d92834023d09910b83dc5504c6cf405d09e6ae7b208a3e5"
    ),
  }
  for option in (
    "--proto '=https'",
    "--proto-redir '=https'",
    "--retry-all-errors",
    "--connect-timeout",
    "--max-time",
    "--retry-max-time",
    "sha256sum --check --strict",
    "ccache version $version",
  ):
    assert option in installer
  assert '\"$(uname -s):$(uname -m)\"' in installer
  assert "Linux:x86_64" in installer
  assert 'echo "$bin_dir" >> "$github_path"' in installer

  for option in (
    "Acquire::Retries=3",
    "Acquire::http::Timeout=30",
    "Acquire::https::Timeout=30",
    "Acquire::Languages=none",
    "--no-install-recommends",
  ):
    assert option in apt
  assert "apt-get update || true" not in workflow_text
  assert not re.search(r"apt-get install[^\n]*\bccache\b", workflow_text)
  assert workflow_text.count("tools/install_ci_ccache.sh") >= 13
  assert "command -v ccache" in stats
  assert "ccache --show-stats" in stats
  assert "tools/report_ccache_stats.sh" in workflow_text


def _write_executable(path: Path, source: str) -> None:
  path.write_text(source)
  path.chmod(0o755)


def _ccache_installer_fixture(tmp_path: Path, digest: str):
  root = Path(__file__).resolve().parents[1]
  fake_bin = tmp_path / "bin"
  fake_bin.mkdir()
  payload = tmp_path / "payload"
  payload.write_bytes(b"pinned ccache fixture")
  lock = tmp_path / "tools.lock.json"
  lock.write_text(json.dumps({
    "ccache": {
      "version": "4.13.6",
      "linux_x86_64": "https://example.invalid/ccache.tar.xz",
      "sha256": digest,
    }
  }))
  _write_executable(
    fake_bin / "uname",
    '#!/bin/sh\n[ "$1" = -s ] && echo Linux || echo x86_64\n',
  )
  _write_executable(
    fake_bin / "curl",
    """#!/bin/sh
if [ "${FAKE_CURL_FAIL:-0}" = 1 ]; then exit 9; fi
while [ "$#" -gt 0 ]; do
  if [ "$1" = --output ]; then cp "$FAKE_PAYLOAD" "$2"; exit 0; fi
  shift
done
exit 8
""",
  )
  _write_executable(
    fake_bin / "tar",
    """#!/bin/sh
if [ "${FAKE_TAR_FAIL:-0}" = 1 ]; then exit 7; fi
while [ "$#" -gt 0 ]; do
  if [ "$1" = --directory ]; then destination=$2; shift; fi
  shift
done
printf '%s\n' '#!/bin/sh' \
  'echo "ccache version ${FAKE_CCACHE_VERSION:-4.13.6}"' \
  > "$destination/ccache"
chmod +x "$destination/ccache"
    """,
  )
  _write_executable(
    fake_bin / "sha256sum",
    "#!/bin/sh\nexec /usr/bin/shasum -a 256 -c\n",
  )
  env = {
    **os.environ,
    "PATH": f"{fake_bin}:{os.environ['PATH']}",
    "FAKE_PAYLOAD": str(payload),
  }
  command = (
    root / "tools" / "install_ci_ccache.sh",
    lock,
    tmp_path / "install",
    tmp_path / "github-path",
  )
  return command, env


@pytest.mark.parametrize(
  ("env_key", "digest"),
  (
    ("FAKE_CURL_FAIL", None),
    ("BAD_DIGEST", "0" * 64),
    ("FAKE_TAR_FAIL", None),
    ("FAKE_CCACHE_VERSION", None),
  ),
)
def test_ci_ccache_installer_fails_closed(tmp_path, env_key, digest):
  import hashlib

  payload_digest = hashlib.sha256(b"pinned ccache fixture").hexdigest()
  command, env = _ccache_installer_fixture(
    tmp_path, digest or payload_digest
  )
  if env_key == "FAKE_CCACHE_VERSION":
    env[env_key] = "4.13.5"
  elif env_key != "BAD_DIGEST":
    env[env_key] = "1"

  result = subprocess.run(command, env=env, capture_output=True, text=True)

  assert result.returncode != 0


def test_ci_ccache_installer_accepts_verified_binary(tmp_path):
  import hashlib

  digest = hashlib.sha256(b"pinned ccache fixture").hexdigest()
  command, env = _ccache_installer_fixture(tmp_path, digest)

  subprocess.run(command, env=env, check=True)

  path = tmp_path / "github-path"
  assert path.read_text().strip() == str(tmp_path / "install" / "bin")


def test_ci_ccache_installer_rejects_unsupported_host(tmp_path):
  root = Path(__file__).resolve().parents[1]
  fake_bin = tmp_path / "bin"
  fake_bin.mkdir()
  _write_executable(
    fake_bin / "uname",
    '#!/bin/sh\n[ "$1" = -s ] && echo Darwin || echo arm64\n',
  )
  result = subprocess.run(
    (
      root / "tools" / "install_ci_ccache.sh",
      root / "ci" / "tools.lock.json",
      tmp_path / "install",
      tmp_path / "github-path",
    ),
    env={**os.environ, "PATH": f"{fake_bin}:{os.environ['PATH']}"},
    capture_output=True,
    text=True,
  )

  assert result.returncode != 0
  assert "Linux x86_64 only" in result.stderr


def test_ci_apt_helper_forwards_options_and_failures(tmp_path):
  root = Path(__file__).resolve().parents[1]
  fake_bin = tmp_path / "bin"
  fake_bin.mkdir()
  log = tmp_path / "sudo.log"
  _write_executable(
    fake_bin / "sudo",
    """#!/bin/sh
printf '%s\n' "$*" >> "$FAKE_SUDO_LOG"
case "$*" in
  *"$FAKE_SUDO_FAIL"*) exit 6 ;;
esac
""",
  )
  base_env = {
    **os.environ,
    "PATH": f"{fake_bin}:{os.environ['PATH']}",
    "FAKE_SUDO_LOG": str(log),
    "FAKE_SUDO_FAIL": "unused",
  }
  command = (root / "tools" / "ci_apt_install.sh", "libc++-dev")

  subprocess.run(command, env=base_env, check=True)
  calls = log.read_text()
  assert calls.count("Acquire::Retries=3") == 2
  assert "apt-get" in calls
  assert " update" in calls
  assert " install --no-install-recommends -y libc++-dev" in calls

  for stage in ("update", "install"):
    env = {**base_env, "FAKE_SUDO_FAIL": stage}
    result = subprocess.run(command, env=env)
    assert result.returncode == 6


def test_ccache_stats_cleanup_distinguishes_missing_and_broken(tmp_path):
  root = Path(__file__).resolve().parents[1]
  script = root / "tools" / "report_ccache_stats.sh"
  bash = shutil.which("bash")
  assert bash is not None
  missing_bin = tmp_path / "missing-bin"
  missing_bin.mkdir()
  missing_path = str(missing_bin)
  assert {"/usr/bin", "/bin"}.isdisjoint(
    missing_path.split(os.pathsep)
  )
  assert missing_path != os.environ["PATH"]
  missing = subprocess.run(
    (bash, script),
    env={**os.environ, "PATH": missing_path},
    capture_output=True,
    text=True,
  )
  assert missing.returncode == 0
  assert "was not installed" in missing.stdout

  fake_bin = tmp_path / "bin"
  fake_bin.mkdir()
  _write_executable(fake_bin / "ccache", "#!/bin/sh\nexit 7\n")
  broken_path = str(fake_bin)
  assert {"/usr/bin", "/bin"}.isdisjoint(
    broken_path.split(os.pathsep)
  )
  assert broken_path != os.environ["PATH"]
  broken = subprocess.run(
    (bash, script),
    env={**os.environ, "PATH": broken_path},
  )
  assert broken.returncode == 7


def test_runner_image_tools_are_verified_instead_of_reinstalled():
  root = Path(__file__).resolve().parents[1]
  workflow = (root / ".github" / "workflows" / "ci.yml").read_text()
  portable = (
    root / ".github" / "workflows" / "portable-headers-release.yml"
  ).read_text()
  advisory = (
    root / ".github" / "workflows" / "advisory.yml"
  ).read_text()

  floors = _job_body(workflow, "release-linux-floors")
  fuzz = _job_body(workflow, "release-fuzz")
  gcc = _job_body(workflow, "gcc")
  quality = _job_body(workflow, "quality")
  tidy = _job_body(workflow, "tidy-diff")
  assert "apt-get" not in floors
  assert "ninja --version" in floors
  assert "clang version 16" in floors
  assert "${{ matrix.version }}" in gcc
  assert "-dumpversion" in gcc
  assert "apt-get" not in fuzz
  assert "clang version 16" in fuzz
  assert "apt-get" not in portable
  assert "clang version 16" in portable
  assert "g++-12" in portable
  for body in (quality, tidy, advisory):
    assert "apt-get install" not in body or "clang-tidy-18" not in body
    assert "clang-tidy-18 --version" in body
