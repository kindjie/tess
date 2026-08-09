#!/usr/bin/env python3
"""Validate build commands quoted in Markdown against the real build files.

The snippet checker byte-synchronizes C++ fences with compiled sources, so
every C++ example in the docs is provably real. Shell and CMake fences had
no equivalent: the docs quote dozens of `cmake --preset ...` and
`--target ...` invocations, and a preset rename or a removed target would
leave a command that fails for the reader with nothing failing in CI
(audit 2026-08-07 D13).

Executing those commands is the wrong gate -- they build the project, and
CI already does that. What breaks in practice is the NAME: the argument
stops resolving. That is checkable statically, which is what this does.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]

# Fenced blocks whose contents are shell or CMake commands. C++ fences are
# the snippet checker's job.
FENCE_RE = re.compile(
  r"^[ \t]*```(?:sh|bash|shell|console|cmake)[ \t]*\n(?P<body>.*?)\n[ \t]*```",
  re.DOTALL | re.MULTILINE,
)
PRESET_RE = re.compile(r"--preset[= ]+([A-Za-z0-9_-]+)")
# `--target a b c` takes several names; stop at the next option or line end.
# `<...>` is included so a placeholder like `tess_bench_<suite>_thresholds`
# is captured whole and skipped, rather than truncated at the angle bracket
# and reported as the fragment `tess_bench_`.
TARGET_RE = re.compile(r"--target[= ]+((?:[A-Za-z0-9_./+<>-]+[ \t]*)+)")

# Placeholders a reader is expected to substitute, not names to resolve.
PLACEHOLDERS = frozenset({"<suite>", "<preset>", "<target>", "NAME"})


class CommandError(Exception):
  """A documented command names something the build files do not define."""


def configure_presets(path: Path) -> set[str]:
  """Every configure-preset name, including those only inherited from."""
  payload = json.loads(path.read_text(encoding="utf-8"))
  names: set[str] = set()
  for key in ("configurePresets", "buildPresets", "testPresets"):
    for entry in payload.get(key, []):
      name = entry.get("name")
      if isinstance(name, str):
        names.add(name)
  return names


def declared_targets(repo_root: Path) -> set[str]:
  """Target names declared anywhere in the tracked CMake files.

  Deliberately permissive: this catches a target that was renamed or
  deleted outright, not one that is conditionally unavailable. A stricter
  check would need a configured build tree, which would make a docs gate
  depend on a build.
  """
  names: set[str] = set()
  pattern = re.compile(
    r"\b(?:add_executable|add_library|add_custom_target)\s*\(\s*([A-Za-z0-9_]+)"
  )
  alias = re.compile(r"\btess_add_\w+\s*\(\s*([A-Za-z0-9_]+)")
  for cmake in repo_root.rglob("CMakeLists.txt"):
    if "build" in cmake.parts or ".venv" in cmake.parts:
      continue
    text = cmake.read_text(encoding="utf-8", errors="replace")
    names.update(pattern.findall(text))
    names.update(alias.findall(text))
  # Threshold targets are generated per suite from the threshold manifests.
  thresholds = repo_root / "bench" / "thresholds"
  if thresholds.is_dir():
    for manifest in thresholds.glob("*.json"):
      suite = manifest.stem.replace("-", "_")
      names.add(f"tess_bench_{suite}_thresholds")
    names.add("tess_bench_all_thresholds")
  return names


def documents(repo_root: Path) -> list[Path]:
  found: list[Path] = []
  for path in sorted(repo_root.rglob("*.md")):
    parts = path.parts
    if "build" in parts or ".venv" in parts or "_deps" in parts:
      continue
    found.append(path)
  return found


def check(repo_root: Path) -> list[str]:
  """Return one problem string per unresolvable documented name."""
  presets = configure_presets(repo_root / "CMakePresets.json")
  targets = declared_targets(repo_root)
  problems: list[str] = []
  for path in documents(repo_root):
    text = path.read_text(encoding="utf-8", errors="replace")
    rel = path.relative_to(repo_root)
    for fence in FENCE_RE.finditer(text):
      body = fence.group("body")
      line = text.count("\n", 0, fence.start()) + 1
      for name in PRESET_RE.findall(body):
        if name in PLACEHOLDERS or name in presets:
          continue
        problems.append(
          f"{rel}:{line}: --preset {name!r} is not in CMakePresets.json"
        )
      for group in TARGET_RE.findall(body):
        for name in group.split():
          placeholder = "<" in name or ">" in name or name.startswith("$")
          if name in PLACEHOLDERS or placeholder or name in targets:
            continue
          problems.append(
            f"{rel}:{line}: --target {name!r} is declared by no CMakeLists.txt"
          )
  return problems


def main(argv: list[str] | None = None) -> int:
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("--repo-root", type=Path, default=REPO_ROOT)
  args = parser.parse_args(argv)

  problems = check(args.repo_root)
  for problem in problems:
    print(f"doc-commands: {problem}", file=sys.stderr)
  if problems:
    print(
      f"doc-commands: {len(problems)} documented command(s) name something "
      f"the build files do not define",
      file=sys.stderr,
    )
    return 1
  print("Documented build commands resolve against the build files.")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
