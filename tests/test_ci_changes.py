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
    "README.md",
    "docs/index.md",
    "docs/for-agents.md",
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
    ("docs/guide.md", "docs/architecture/path.md", "mkdocs.yml")
  )

  assert not classification.code_required
  assert classification.reason == "documentation-only change"


def test_one_code_path_requires_full_ci():
  classification = ci_changes.classify_paths(
    ("docs/guide.md", "include/tess/tess.h")
  )

  assert classification.code_required
  assert classification.reason == "code-affecting path: 'include/tess/tess.h'"


def test_hook_backstop_runs_branding_asset_regressions():
  workflow = (
    Path(__file__).resolve().parents[1] / ".github" / "workflows" / "ci.yml"
  ).read_text(encoding="utf-8")

  assert "tests/test_branding_assets.py" in workflow


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
  assert "code_required=false\n" in captured.out
  assert "tsan_required=true\n" in captured.out
  assert (
    "CI change classification: documentation-only change\n" in captured.err
  )


# --- Concurrency-sensitive classification for the path-filtered TSan job ---


@pytest.mark.parametrize(
  "path",
  (
    "include/tess/ops/phase_executor.h",
    "include/tess/ops/result_channel.h",
    "include/tess/ops/queued.h",
    "include/tess/experimental/maintenance.h",
    "include/tess/gpu/webgpu_backend.h",
    "include/tess/diagnostics/trace.h",
    "include/tess/sim/schedule.h",
    "include/tess/sim/scheduler.h",
    "include/tess/sim/auto_exec.h",
    "include/tess/sim/async_work_task.h",
    "include/tess/simulation.h",
    "include/tess/tess.h",
    "tests/tess_phase_executor_test.cc",
    "tests/tess_queued_contract_test.cc",
    "tests/tess_sim_schedule_test.cc",
    "tests/tess_sim_scheduler_test.cc",
    "tests/tess_maintenance_test.cc",
    "tests/tess_msvc_exception_mode_spike.cc",
    "tests/tess_no_exceptions_test.cc",
    "tests/tess_webgpu_backend_test.cc",
    "tests/tess_sim_auto_exec_test.cc",
    "tests/tess_execution_phase_safety_test.cc",
    "tests/allocation_counter.cc",
    "tests/webgpu_stub/webgpu.h",
    "tests/CMakeLists.txt",
    "cmake/TessProjectOptions.cmake",
    "CMakeLists.txt",
    "CMakePresets.json",
    ".github/workflows/ci.yml",
    "tools/ci_changes.py",
  ),
)
def test_concurrency_sensitive_paths_select_tsan(path):
  assert ci_changes.is_concurrency_sensitive_path(path)


@pytest.mark.parametrize(
  "path",
  (
    "include/tess/path/astar.h",
    "include/tess/sim/movement.h",
    "include/tess/sim/pibt_movement.h",
    "include/tess/storage/dense.h",
    "docs/index.md",
    "tests/tess_shape_test.cc",
    "examples/quickstart.cc",
    "bench/CMakeLists.txt",
    "tools/check_docs_links.py",
  ),
)
def test_concurrency_insensitive_paths_skip_tsan(path):
  assert not ci_changes.is_concurrency_sensitive_path(path)


def test_files_owning_concurrency_primitives_are_sensitive():
  """A new threaded header or test must not miss the TSan path filter."""
  import re as _re

  repo = Path(__file__).resolve().parents[1]
  pattern = _re.compile(
    r"std::(jthread|thread|mutex|shared_mutex|scoped_lock|unique_lock"
    r"|lock_guard|atomic|condition_variable|future|async|promise"
    r"|stop_token|barrier|latch|counting_semaphore|binary_semaphore)\b"
  )
  candidates = [
    *(repo / "include" / "tess").rglob("*.h"),
    *(repo / "tests").rglob("*.h"),
    *(repo / "tests").rglob("*.cc"),
  ]
  offenders = [
    source.relative_to(repo).as_posix()
    for source in candidates
    if pattern.search(source.read_text(encoding="utf-8"))
    and not ci_changes.is_concurrency_sensitive_path(
      source.relative_to(repo).as_posix()
    )
  ]

  assert offenders == []


def test_tsan_classification_selects_on_sensitive_path():
  classification = ci_changes.classify_tsan_paths(
    ("include/tess/path/astar.h", "include/tess/ops/queued.h")
  )

  assert classification.tsan_required
  assert "include/tess/ops/queued.h" in classification.reason


def test_tsan_classification_skips_without_sensitive_paths():
  classification = ci_changes.classify_tsan_paths(
    ("include/tess/path/astar.h", "docs/index.md")
  )

  assert not classification.tsan_required
  assert classification.reason == "no concurrency-sensitive changes"


def test_tsan_classification_fails_closed_on_empty_change_set():
  assert ci_changes.classify_tsan_paths(()).tsan_required


def test_tsan_range_fails_closed_on_invalid_revision():
  classification = ci_changes.classify_tsan_range("", SHA_B)

  assert classification.tsan_required
  assert classification.reason == "invalid comparison revision"


def test_tsan_range_fails_closed_on_git_failure():
  def fail(_command, **_kwargs):
    raise subprocess.CalledProcessError(128, "git diff")

  classification = ci_changes.classify_tsan_range(SHA_A, SHA_B, run=fail)

  assert classification.tsan_required
  assert classification.reason == "unable to inspect changed paths"


# --- Quality-gate preset selection per event ---


@pytest.mark.parametrize("event", ("push", "schedule", "workflow_dispatch"))
def test_full_tier_events_run_every_quality_preset(event):
  assert ci_changes.quality_presets(event, tsan_required=False) == (
    "dev-werror",
    "dev-asan",
    "dev-tsan",
    "dev-cppcheck",
    "dev-clang-tidy",
    "release",
  )


def test_pull_request_runs_reduced_quality_presets():
  assert ci_changes.quality_presets("pull_request", tsan_required=False) == (
    "dev-asan",
    "dev-cppcheck",
  )


def test_pull_request_adds_tsan_when_required():
  assert ci_changes.quality_presets("pull_request", tsan_required=True) == (
    "dev-asan",
    "dev-cppcheck",
    "dev-tsan",
  )


def test_cli_emits_all_outputs_for_pull_request(monkeypatch, capsys):
  monkeypatch.setattr(
    ci_changes,
    "changed_paths",
    lambda _base, _head, run=None: ("include/tess/path/astar.h",),
  )

  assert ci_changes.main((SHA_A, SHA_B, "--event", "pull_request")) == 0
  captured = capsys.readouterr()
  assert captured.out == (
    "code_required=true\n"
    "tsan_required=false\n"
    "perf_required=true\n"
    'quality_presets=["dev-asan", "dev-cppcheck"]\n'
  )


def test_cli_fails_closed_for_full_tier_events(monkeypatch, capsys):
  monkeypatch.setattr(
    ci_changes,
    "changed_paths",
    lambda _base, _head, run=None: ("docs/guide.md",),
  )

  assert ci_changes.main((SHA_A, SHA_B, "--event", "push")) == 0
  captured = capsys.readouterr()
  assert captured.out == (
    "code_required=false\n"
    "tsan_required=true\n"
    "perf_required=true\n"
    'quality_presets=["dev-werror", "dev-asan", "dev-tsan", '
    '"dev-cppcheck", "dev-clang-tidy", "release"]\n'
  )


# --- Perf-sensitive classification for the paired sentinel job ---


@pytest.mark.parametrize(
  "path",
  (
    "include/tess/path/astar.h",
    "include/tess/storage/dense.h",
    "include/tess/simulation.h",
    "bench/tess_path_bench.cc",
    "bench/sentinels.json",
    "cmake/TessProjectOptions.cmake",
    "CMakeLists.txt",
    "CMakePresets.json",
    ".github/workflows/ci.yml",
    "tools/ci_changes.py",
    "tools/paired_bench.py",
  ),
)
def test_perf_sensitive_paths_select_the_paired_run(path):
  assert ci_changes.is_perf_sensitive_path(path)


@pytest.mark.parametrize(
  "path",
  (
    "docs/index.md",
    "tests/tess_shape_test.cc",
    "examples/quickstart.cc",
    "tools/check_docs_links.py",
    # Directories no sentinel can observe: running the paired job on
    # these would measure nothing. The source map records the gap.
    "include/tess/ops/queued.h",
    "include/tess/gpu/webgpu_backend.h",
    "include/tess/debug/imgui/panels.h",
    "include/tess/diagnostics/trace.h",
    "include/tess/experimental/maintenance.h",
  ),
)
def test_perf_insensitive_paths_skip_the_paired_run(path):
  assert not ci_changes.is_perf_sensitive_path(path)


def test_perf_classification_selects_on_sensitive_path():
  classification = ci_changes.classify_perf_paths(
    ("docs/index.md", "include/tess/path/astar.h")
  )

  assert classification.perf_required
  assert "include/tess/path/astar.h" in classification.reason


def test_perf_classification_skips_without_sensitive_paths():
  classification = ci_changes.classify_perf_paths(
    ("docs/index.md", "tests/tess_shape_test.cc")
  )

  assert not classification.perf_required


def test_perf_classification_fails_closed_on_empty_change_set():
  assert ci_changes.classify_perf_paths(()).perf_required


def test_perf_range_fails_closed_on_invalid_revision():
  assert ci_changes.classify_perf_range("", SHA_B).perf_required


def test_cli_emits_perf_required(monkeypatch, capsys):
  monkeypatch.setattr(
    ci_changes,
    "changed_paths",
    lambda _base, _head, run=None: ("include/tess/path/astar.h",),
  )

  assert ci_changes.main((SHA_A, SHA_B, "--event", "pull_request")) == 0
  captured = capsys.readouterr()
  assert "perf_required=true\n" in captured.out
