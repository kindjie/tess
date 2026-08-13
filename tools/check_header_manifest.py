#!/usr/bin/env python3
"""Validate exhaustive header stability classes and aggregate boundaries."""

from __future__ import annotations

import argparse
import posixpath
import re
from pathlib import Path

from header_manifest import GENERATED_HEADER_SOURCES, HEADER_CLASSES
from header_manifest import load_header_manifest

REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_MANIFEST = REPO_ROOT / "cmake" / "tess-headers.json"
STABLE_AGGREGATES = (
    "include/tess/pathfinding.h",
    "include/tess/simulation.h",
    "include/tess/tess.h",
)
INCLUDE_RE = re.compile(
    r'^\s*#\s*include\s*(?:<([^>\n]+)>|"([^"\n]+)")',
    re.MULTILINE,
)


def check_manifest(repo_root: Path, manifest_path: Path) -> list[str]:
  """Return deterministic failures for the header stability manifest."""
  manifest = load_header_manifest(manifest_path)
  failures: list[str] = []
  owners: dict[str, str] = {}
  for category in HEADER_CLASSES:
    for header in manifest[category]:
      if header in owners:
        failures.append(
            f"{header}: classified as both {owners[header]} and {category}"
        )
      else:
        owners[header] = category
      source = GENERATED_HEADER_SOURCES.get(header, header)
      if not (repo_root / source).is_file():
        failures.append(f"{header}: classified header not found")

  actual = {
      path.relative_to(repo_root).as_posix()
      for path in (repo_root / "include" / "tess").rglob("*.h")
  }
  actual.update(
      header
      for header, source in GENERATED_HEADER_SOURCES.items()
      if (repo_root / source).is_file()
  )
  for header in sorted(actual - owners.keys()):
    failures.append(f"{header}: installed header is unclassified")
  for header in sorted(owners.keys() - actual):
    failures.append(f"{header}: classification is stale")

  forbidden = set(manifest["experimental"]) | set(
      manifest["implementation-only"]
  )
  for aggregate in STABLE_AGGREGATES:
    text = (repo_root / aggregate).read_text(encoding="utf-8")
    includes = {
        (
            f"include/{angle}"
            if angle
            else (
                f"include/{quote}"
                if quote.startswith("tess/")
                else posixpath.normpath(
                    (Path(aggregate).parent / quote).as_posix()
                )
            )
        )
        for angle, quote in INCLUDE_RE.findall(text)
    }
    for imported in sorted(includes & forbidden):
      failures.append(
          f"{aggregate}: stable aggregate imports {owners[imported]} "
          f"header {imported}"
      )
  return failures


def main() -> int:
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("--repo-root", type=Path, default=REPO_ROOT)
  parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
  args = parser.parse_args()
  failures = check_manifest(args.repo_root, args.manifest)
  if failures:
    print("\n".join(failures))
    return 1
  print("header stability manifest is exhaustive and aggregate-safe")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
