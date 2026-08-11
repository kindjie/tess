#!/usr/bin/env python3
"""Run clang-tidy on the translation units a revision range touches.

The blocking pull-request clang-tidy gate: changed sources are checked
through the compilation database, and changed headers — most of this
header-only library — through a real database translation unit whose
resolved include closure reaches them and whose compile command defines
the feature macro a gated header needs, falling back to a synthesized
one-include translation unit when no configured consumer exists yet.
Analyzer or build configuration changes add a representative reference
check so the gate never passes with zero checks on a change that could
alter diagnostics. The full-tree sweep runs on main; this tool trades
its breadth for pull-request latency without losing coverage on the
changed surface.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import json
import re
import subprocess
import sys
from collections.abc import Iterable, Mapping, Sequence
from dataclasses import dataclass
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parent))

from ci_changes import changed_paths, valid_revision  # noqa: E402


HEADER_PREFIXES = ("include/", "tests/")
SOURCE_PREFIXES = ("tests/", "examples/")
# Parity with the full-tree dev-clang-tidy job: the dev preset builds no
# benchmarks and no webgpu_stub translation units, so this gate does not
# introduce coverage the full tree never had.
EXCLUDED_PREFIXES = ("bench/", "tests/webgpu_stub/")
# Sources deliberately outside the dev preset's compilation database:
# standalone consumer-smoke projects and the real-WebGPU example (whole
# directories), the opt-in libFuzzer harness, the bench-preset-only data test,
# and the exception-free-only runtime test (exact files).
# Anything else missing from the database fails the gate — a new or
# renamed source must not silently evade the only blocking pull-request
# clang-tidy job.
GATED_SOURCE_DIRECTORIES = (
  "examples/webgpu_compute/",
  "tests/fetchcontent_consumer/",
  "tests/install_consumer/",
)
GATED_SOURCE_FILES = (
  "tests/no_exceptions_consumer_contract_main.cc",
  "tests/fuzz/tess_world_archive_fuzzer.cc",
  "tests/tess_grid_benchmark_data_test.cc",
  "tests/tess_no_exceptions_test.cc",
)
# Changes that can alter clang-tidy's behavior or the analyzed surface
# without touching any checkable source; they trigger a representative
# reference check.
CONFIG_TRIGGER_FILES = (
  ".clang-tidy",
  ".github/workflows/ci.yml",
  "CMakePresets.json",
  "include/tess/version.h.in",
  "tools/clang_tidy_changed.py",
)
CONFIG_TRIGGER_DIRECTORIES = ("cmake/",)
# Feature-gated directories and the macro a consumer's compile command
# must define for clang-tidy to analyze the gated code at all.
FEATURE_MACRO_DIRECTORIES = (
  ("include/tess/debug/", "TESS_ENABLE_IMGUI"),
  ("include/tess/diagnostics/", "TESS_ENABLE_DIAGNOSTICS"),
  ("include/tess/ecs/entt/", "TESS_ENABLE_ENTT"),
  ("include/tess/ecs/flecs/", "TESS_ENABLE_FLECS"),
  ("include/tess/gpu/", "TESS_ENABLE_WEBGPU"),
)

INCLUDE_RE = re.compile(r'^\s*#\s*include\s+["<]([^">]+)[">]', re.MULTILINE)


class ToolError(RuntimeError):
  """A configuration failure that must fail the gate, not skip it."""


@dataclass(frozen=True)
class Candidates:
  """Changed paths split by how clang-tidy must consume them."""

  headers: tuple[str, ...]
  sources: tuple[str, ...]


@dataclass(frozen=True)
class Check:
  """One clang-tidy invocation and the path it reports against."""

  path: str
  command: tuple[str, ...]
  tu_path: str = ""


def _deduplicated(paths: Iterable[str]) -> tuple[str, ...]:
  return tuple(dict.fromkeys(paths))


def select_candidates(paths: Iterable[str]) -> Candidates:
  """Split changed paths into header and source clang-tidy candidates."""
  headers = []
  sources = []
  for path in paths:
    if path.startswith(EXCLUDED_PREFIXES):
      continue
    if path.endswith(".h") and path.startswith(HEADER_PREFIXES):
      headers.append(path)
    elif path.endswith(".cc") and path.startswith(SOURCE_PREFIXES):
      sources.append(path)
  return Candidates(_deduplicated(headers), _deduplicated(sources))


def is_config_trigger(path: str) -> bool:
  """Return whether a change requires the representative check."""
  return (
    path in CONFIG_TRIGGER_FILES
    or path.startswith(CONFIG_TRIGGER_DIRECTORIES)
    or path.endswith("CMakeLists.txt")
  )


def source_disposition(path: str, *, in_database: bool) -> str:
  """Return 'check', 'skip', or 'fail' for an existing changed source."""
  if in_database:
    return "check"
  if path.startswith(GATED_SOURCE_DIRECTORIES) or path in GATED_SOURCE_FILES:
    return "skip"
  return "fail"


def required_macro(path: str) -> str | None:
  """Return the feature macro a header's consumer must define, if any."""
  for directory, macro in FEATURE_MACRO_DIRECTORIES:
    if path.startswith(directory):
      return macro
  return None


def entry_defines(entry: dict[str, Any], macro: str) -> bool:
  """Return whether a database entry's compile command defines a macro."""
  if "arguments" in entry:
    command = " ".join(entry["arguments"])
  else:
    command = entry["command"]
  return macro in command


def resolve_include(target: str, repo_root: Path) -> str | None:
  """Map an include string to the repo-relative file it names, if any."""
  for prefix in ("include", "tests"):
    candidate = repo_root / prefix / target
    if candidate.is_file():
      return f"{prefix}/{target}"
  return None


def read_direct_includes(
  files: Iterable[Path],
  repo_root: Path,
) -> dict[str, tuple[str, ...]]:
  """Map repo-relative files to the repo files they directly include."""
  includes = {}
  for source in files:
    try:
      text = source.read_text(encoding="utf-8")
    except OSError:
      continue
    relative = source.relative_to(repo_root).as_posix()
    resolved = (
      resolve_include(target, repo_root)
      for target in INCLUDE_RE.findall(text)
    )
    includes[relative] = tuple(
      target for target in resolved if target is not None
    )
  return includes


def include_closure(
  start: Iterable[str],
  graph: Mapping[str, tuple[str, ...]],
) -> frozenset[str]:
  """Return every file reachable through the resolved include graph."""
  seen = set()
  stack = list(start)
  while stack:
    node = stack.pop()
    if node in seen:
      continue
    seen.add(node)
    stack.extend(graph.get(node, ()))
  return frozenset(seen)


def find_consumer(
  header: str,
  tu_entries: Mapping[str, dict[str, Any]],
  graph: Mapping[str, tuple[str, ...]],
) -> str | None:
  """Find a database translation unit whose closure reaches a header.

  Textual include edges ignore preprocessor gating, so a feature-gated
  header additionally requires the consumer's compile command to define
  its macro — otherwise the preprocessor drops the gated code and the
  check would silently analyze nothing. Test translation units are
  preferred over examples. Returns None when no configured consumer
  reaches the header.
  """
  macro = required_macro(header)
  ordered = sorted(
    tu_entries,
    key=lambda path: (not path.startswith("tests/"), path),
  )
  for path in ordered:
    if macro is not None and not entry_defines(tu_entries[path], macro):
      continue
    if header in include_closure(graph.get(path, ()), graph):
      return path
  return None


def reference_compile_flags(entry: dict[str, Any]) -> tuple[str, ...]:
  """Extract reusable compile flags from a compilation-database entry."""
  if "arguments" in entry:
    arguments = list(entry["arguments"])
  else:
    arguments = entry["command"].split()
  flags = []
  skip_next = False
  source = entry["file"]
  for argument in arguments[1:]:
    if skip_next:
      skip_next = False
      continue
    if argument == "-o":
      skip_next = True
      continue
    if argument == "-c" or argument == source:
      continue
    flags.append(argument)
  return tuple(flags)


def database_index(
  entries: Iterable[dict[str, Any]],
) -> dict[Path, dict[str, Any]]:
  """Map absolute source paths to their compilation-database entries."""
  index = {}
  for entry in entries:
    file_path = Path(entry["file"])
    if not file_path.is_absolute():
      file_path = Path(entry["directory"]) / file_path
    index[file_path.resolve()] = entry
  return index


def reference_entry(
  entries: Sequence[dict[str, Any]],
  repo_root: Path,
) -> dict[str, Any]:
  """Pick the entry whose flags header checks borrow, failing closed."""
  tests_dir = (repo_root / "tests").resolve()
  test_entries = [
    entry
    for path, entry in sorted(database_index(entries).items())
    if path.is_relative_to(tests_dir)
  ]
  if not test_entries:
    raise ToolError("no test translation unit in the compilation database")
  for entry in test_entries:
    if Path(entry["file"]).name == "tess_smoke.cc":
      return entry
  return test_entries[0]


def header_check(
  path: str,
  *,
  repo_root: Path,
  scratch_dir: Path,
  flags: tuple[str, ...],
  clang_tidy: str,
) -> Check:
  """Synthesize a one-include translation unit and its check command."""
  scratch_dir.mkdir(parents=True, exist_ok=True)
  tu = scratch_dir / (path.replace("/", "_") + ".cc")
  tu.write_text(f'#include "{repo_root / path}"\n', encoding="utf-8")
  command = (clang_tidy, "--quiet", str(tu), "--", *flags)
  return Check(path=path, command=command, tu_path=str(tu))


def source_check(
  path: str,
  *,
  repo_root: Path,
  build_dir: Path,
  clang_tidy: str,
) -> Check:
  """Build the database-backed check command for a changed source."""
  command = (
    clang_tidy,
    "--quiet",
    "-p",
    str(build_dir),
    str(repo_root / path),
  )
  return Check(path=path, command=command)


def run_checks(checks: Sequence[Check], jobs: int) -> int:
  """Run checks concurrently; report each result; return failure count."""
  failures = 0
  with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
    futures = {
      pool.submit(
        subprocess.run,
        check.command,
        capture_output=True,
        text=True,
      ): check
      for check in checks
    }
    for future in concurrent.futures.as_completed(futures):
      check = futures[future]
      result = future.result()
      verdict = "clean" if result.returncode == 0 else "FAILED"
      print(f"clang-tidy {verdict}: {check.path}", flush=True)
      output = (result.stdout + result.stderr).strip()
      if result.returncode != 0:
        failures += 1
        print(output, flush=True)
  return failures


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("base", help="full base commit object ID")
  parser.add_argument("head", help="full head commit object ID")
  parser.add_argument(
    "--build-dir",
    type=Path,
    required=True,
    help="configured build directory containing compile_commands.json",
  )
  parser.add_argument("--jobs", type=int, default=4)
  parser.add_argument("--clang-tidy", default="clang-tidy")
  parser.add_argument(
    "--repo-root",
    type=Path,
    default=Path(__file__).resolve().parents[1],
    help=argparse.SUPPRESS,
  )
  return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
  args = parse_args(sys.argv[1:] if argv is None else argv)
  if not valid_revision(args.base) or not valid_revision(args.head):
    print("error: invalid comparison revision", file=sys.stderr)
    return 1
  repo_root = args.repo_root.resolve()
  database_path = args.build_dir / "compile_commands.json"
  if not database_path.is_file():
    print(f"error: {database_path} not found", file=sys.stderr)
    return 1
  entries = json.loads(database_path.read_text(encoding="utf-8"))

  paths = changed_paths(args.base, args.head)
  candidates = select_candidates(paths)
  index = database_index(entries)
  tu_entries = {
    path.relative_to(repo_root).as_posix(): entry
    for path, entry in index.items()
    if path.is_relative_to(repo_root) and path.suffix == ".cc"
  }
  graph_files = [
    *(repo_root / "include").rglob("*.h"),
    *(repo_root / "tests").rglob("*.h"),
    *(repo_root / path for path in tu_entries),
  ]
  graph = read_direct_includes(graph_files, repo_root)

  tu_paths = []
  synthesized = []
  missing = []
  for path in candidates.headers:
    if not (repo_root / path).is_file():
      continue  # deleted in this range
    consumer = find_consumer(path, tu_entries, graph)
    if consumer is None:
      if required_macro(path) is not None:
        print(
          f"warning: {path} is feature-gated with no configured "
          "consumer; the synthesized check cannot analyze gated code"
        )
      synthesized.append(path)
    else:
      print(f"checking {path} through {consumer}")
      tu_paths.append(consumer)
  for path in candidates.sources:
    if not (repo_root / path).is_file():
      continue  # deleted in this range
    disposition = source_disposition(
      path,
      in_database=(repo_root / path).resolve() in index,
    )
    if disposition == "check":
      tu_paths.append(path)
    elif disposition == "skip":
      print(f"skipping {path}: gated outside the dev preset")
    else:
      missing.append(path)
  if missing:
    for path in missing:
      print(
        f"error: {path} has no compilation-database entry; wire it into "
        "the dev preset or add a documented gated exclusion",
        file=sys.stderr,
      )
    return 1
  if any(is_config_trigger(path) for path in paths):
    reference = reference_entry(entries, repo_root)
    representative = (
      Path(reference["file"]).resolve().relative_to(repo_root).as_posix()
    )
    print(f"configuration change: adding representative {representative}")
    tu_paths.append(representative)

  checks = [
    source_check(
      path,
      repo_root=repo_root,
      build_dir=args.build_dir,
      clang_tidy=args.clang_tidy,
    )
    for path in _deduplicated(tu_paths)
  ]
  checks += [
    header_check(
      path,
      repo_root=repo_root,
      scratch_dir=args.build_dir / "clang-tidy-changed",
      flags=reference_compile_flags(reference_entry(entries, repo_root)),
      clang_tidy=args.clang_tidy,
    )
    for path in synthesized
  ]

  if not checks:
    print("no clang-tidy candidates changed")
    return 0
  print(f"checking {len(checks)} translation unit(s)")
  failures = run_checks(checks, jobs=max(1, args.jobs))
  return 1 if failures else 0


if __name__ == "__main__":
  raise SystemExit(main())
