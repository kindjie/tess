#!/usr/bin/env python3
"""Classify a Git revision range for tiered CI job selection.

Emits three fail-closed outputs for ``$GITHUB_OUTPUT``: whether the
change needs code CI at all (the documentation-only fast path), whether
it touches concurrency-sensitive paths (the path-filtered TSan gate on
pull requests), and which quality-gate presets the event tier runs.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from collections.abc import Callable, Iterable, Sequence
from dataclasses import dataclass


REVISION_RE = re.compile(r"(?:[0-9a-fA-F]{40}|[0-9a-fA-F]{64})")
EXECUTABLE_OUTPUT_DOCS = {
  "README.md",
  "docs/for-agents.md",
  "docs/index.md",
}

# The curated concurrency surface for the path-filtered TSan job. When a
# header gains a standard threading primitive, add it here;
# tests/test_ci_changes.py scans include/tess/ for primitives and fails
# if one is missing. Composition risk (a serial-looking header executed
# under the parallel phase backend) is covered by TSan-always on main.
CONCURRENCY_SENSITIVE_DIRECTORIES = (
  ".github/workflows/",
  "cmake/",
  "include/tess/diagnostics/",
  "include/tess/experimental/",
  "include/tess/gpu/",
  "include/tess/ops/",
  "tests/webgpu_stub/",
)
CONCURRENCY_SENSITIVE_FILES = (
  "CMakeLists.txt",
  "CMakePresets.json",
  "include/tess/sim/async_work_task.h",
  "include/tess/sim/auto_exec.h",
  "include/tess/sim/schedule.h",
  "include/tess/sim/scheduler.h",
  "include/tess/simulation.h",
  "include/tess/tess.h",
  "tests/CMakeLists.txt",
  "tools/ci_changes.py",
)
CONCURRENCY_SENSITIVE_TEST_PREFIXES = (
  "tests/allocation_counter",
  "tests/tess_execution_phase_safety",
  "tests/tess_maintenance",
  "tests/tess_no_exceptions",
  "tests/tess_phase_executor",
  "tests/tess_queued",
  "tests/tess_sim_auto_exec",
  "tests/tess_sim_schedule",
  "tests/tess_webgpu",
)

# Paths whose changes can move what the paired sentinel run measures:
# the library itself, the benchmarks, anything that changes compile
# flags, and the paired run's own orchestration (self-validation).
# Library directories that no sentinel can observe are excluded — the
# sentinel source map in bench/sentinels.json must declare exactly
# these as unrepresented, and tests/test_paired_bench.py enforces that
# the two lists agree.
PERF_SENSITIVE_PREFIXES = (
  ".github/workflows/",
  "bench/",
  "cmake/",
  "include/tess/",
)
PERF_INSENSITIVE_OVERRIDES = (
  "include/tess/debug/",
  "include/tess/diagnostics/",
  "include/tess/experimental/",
  "include/tess/gpu/",
  "include/tess/ops/",
)
PERF_SENSITIVE_FILES = (
  "CMakeLists.txt",
  "CMakePresets.json",
  "tools/ci_changes.py",
  "tools/paired_bench.py",
)

FULL_QUALITY_PRESETS = (
  "dev-werror",
  "dev-asan",
  "dev-tsan",
  "dev-cppcheck",
  "dev-clang-tidy",
  "release",
)
PULL_REQUEST_QUALITY_PRESETS = (
  "dev-asan",
  "dev-cppcheck",
)


@dataclass(frozen=True)
class Classification:
  """A fail-closed CI classification and its human-readable reason."""

  code_required: bool
  reason: str


def is_documentation_path(path: str) -> bool:
  """Return whether a path is covered by documentation-specific checks."""
  # These pages embed tess-output fences checked against a compiled example.
  # The documentation-only fast path cannot validate them, so changes must
  # select the dev job that builds and runs the authoritative executable.
  if path in EXECUTABLE_OUTPUT_DOCS:
    return False
  return (
    path == "mkdocs.yml"
    or path.startswith("docs/")
    or path.lower().endswith(".md")
  )


def classify_paths(paths: Iterable[str]) -> Classification:
  """Require full CI unless every changed path is documentation-only."""
  changed = tuple(paths)
  if not changed:
    return Classification(True, "no changed paths found")
  for path in changed:
    if not is_documentation_path(path):
      return Classification(True, f"code-affecting path: {path!r}")
  return Classification(False, "documentation-only change")


@dataclass(frozen=True)
class TsanClassification:
  """A fail-closed TSan selection and its human-readable reason."""

  tsan_required: bool
  reason: str


def is_concurrency_sensitive_path(path: str) -> bool:
  """Return whether a path selects the pull-request TSan gate."""
  return (
    path.startswith(CONCURRENCY_SENSITIVE_DIRECTORIES)
    or path in CONCURRENCY_SENSITIVE_FILES
    or path.startswith(CONCURRENCY_SENSITIVE_TEST_PREFIXES)
  )


def classify_tsan_paths(paths: Iterable[str]) -> TsanClassification:
  """Require TSan unless no changed path is concurrency-sensitive."""
  changed = tuple(paths)
  if not changed:
    return TsanClassification(True, "no changed paths found")
  for path in changed:
    if is_concurrency_sensitive_path(path):
      return TsanClassification(True, f"concurrency-sensitive path: {path!r}")
  return TsanClassification(False, "no concurrency-sensitive changes")


def quality_presets(event: str, *, tsan_required: bool) -> tuple[str, ...]:
  """Return the quality-gate matrix presets for an event tier."""
  if event == "pull_request":
    if tsan_required:
      return PULL_REQUEST_QUALITY_PRESETS + ("dev-tsan",)
    return PULL_REQUEST_QUALITY_PRESETS
  return FULL_QUALITY_PRESETS


def valid_revision(revision: str) -> bool:
  """Accept a full, nonzero hexadecimal Git object ID."""
  return bool(REVISION_RE.fullmatch(revision)) and set(revision) != {"0"}


def changed_paths(
  base: str,
  head: str,
  *,
  run: Callable[..., subprocess.CompletedProcess[bytes]] = subprocess.run,
) -> tuple[str, ...]:
  """Return every path in a range, treating renames as delete plus add."""
  command = (
    "git",
    "diff",
    "--name-only",
    "--no-renames",
    "-z",
    base,
    head,
    "--",
  )
  result = run(command, check=True, stdout=subprocess.PIPE)
  return tuple(
    item.decode("utf-8", errors="surrogateescape")
    for item in result.stdout.split(b"\0")
    if item
  )


def classify_range(
  base: str,
  head: str,
  *,
  run: Callable[..., subprocess.CompletedProcess[bytes]] = subprocess.run,
) -> Classification:
  """Classify a range, requiring full CI when inspection is unreliable."""
  if not valid_revision(base) or not valid_revision(head):
    return Classification(True, "invalid comparison revision")
  try:
    paths = changed_paths(base, head, run=run)
  except (OSError, subprocess.CalledProcessError):
    return Classification(True, "unable to inspect changed paths")
  return classify_paths(paths)


def classify_tsan_range(
  base: str,
  head: str,
  *,
  run: Callable[..., subprocess.CompletedProcess[bytes]] = subprocess.run,
) -> TsanClassification:
  """Classify a range for TSan, failing closed when inspection fails."""
  if not valid_revision(base) or not valid_revision(head):
    return TsanClassification(True, "invalid comparison revision")
  try:
    paths = changed_paths(base, head, run=run)
  except (OSError, subprocess.CalledProcessError):
    return TsanClassification(True, "unable to inspect changed paths")
  return classify_tsan_paths(paths)


@dataclass(frozen=True)
class PerfClassification:
  """A fail-closed paired-run selection and its human-readable reason."""

  perf_required: bool
  reason: str


def is_perf_sensitive_path(path: str) -> bool:
  """Return whether a path selects the paired sentinel benchmark run."""
  if path.startswith(PERF_INSENSITIVE_OVERRIDES):
    return False
  return (
    path.startswith(PERF_SENSITIVE_PREFIXES)
    or path in PERF_SENSITIVE_FILES
  )


def classify_perf_paths(paths: Iterable[str]) -> PerfClassification:
  """Require the paired run unless no changed path is perf-sensitive."""
  changed = tuple(paths)
  if not changed:
    return PerfClassification(True, "no changed paths found")
  for path in changed:
    if is_perf_sensitive_path(path):
      return PerfClassification(True, f"perf-sensitive path: {path!r}")
  return PerfClassification(False, "no perf-sensitive changes")


def classify_perf_range(
  base: str,
  head: str,
  *,
  run: Callable[..., subprocess.CompletedProcess[bytes]] = subprocess.run,
) -> PerfClassification:
  """Classify a range for the paired run, failing closed on errors."""
  if not valid_revision(base) or not valid_revision(head):
    return PerfClassification(True, "invalid comparison revision")
  try:
    paths = changed_paths(base, head, run=run)
  except (OSError, subprocess.CalledProcessError):
    return PerfClassification(True, "unable to inspect changed paths")
  return classify_perf_paths(paths)


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("base", help="full base commit object ID")
  parser.add_argument("head", help="full head commit object ID")
  parser.add_argument(
    "--event",
    default="",
    help="GitHub event name; anything but pull_request runs the full tier",
  )
  return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
  args = parse_args(sys.argv[1:] if argv is None else argv)
  classification = classify_range(args.base, args.head)
  if args.event == "pull_request":
    tsan = classify_tsan_range(args.base, args.head)
  else:
    tsan = TsanClassification(True, "full-tier event runs TSan directly")
  presets = quality_presets(args.event, tsan_required=tsan.tsan_required)
  if args.event == "pull_request":
    perf = classify_perf_range(args.base, args.head)
  else:
    perf = PerfClassification(True, "full-tier event")
  print(f"code_required={str(classification.code_required).lower()}")
  print(f"tsan_required={str(tsan.tsan_required).lower()}")
  print(f"perf_required={str(perf.perf_required).lower()}")
  print(f"quality_presets={json.dumps(list(presets))}")
  print(
    f"CI change classification: {classification.reason}",
    file=sys.stderr,
  )
  print(
    f"CI TSan classification: {tsan.reason}",
    file=sys.stderr,
  )
  print(
    f"CI perf classification: {perf.reason}",
    file=sys.stderr,
  )
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
