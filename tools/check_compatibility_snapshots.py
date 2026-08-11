#!/usr/bin/env python3
"""Validate immutable 1.x compatibility snapshots against current sources."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

from check_public_surface import extract_public_symbols
from header_manifest import GENERATED_HEADER_SOURCES, load_header_manifest

REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SNAPSHOTS = REPO_ROOT / "compatibility"
DEFAULT_HEADERS = REPO_ROOT / "cmake" / "tess-headers.json"
DEFAULT_VERSION = REPO_ROOT / "cmake" / "tess-version.cmake"
AGGREGATES = (
    "include/tess/pathfinding.h",
    "include/tess/simulation.h",
    "include/tess/tess.h",
)
INCLUDE_RE = re.compile(r"^\s*#\s*include\s*<([^>]+)>", re.MULTILINE)
VERSION_RE = re.compile(
    r'^set\(TESS_VERSION\s+"?([^"\s)]+)"?\)', re.MULTILINE
)
PRERELEASE_RE = re.compile(
    r'^set\(TESS_VERSION_PRERELEASE "([^"]*)"\)', re.MULTILINE
)


def current_version(path: Path) -> str:
  """Return the complete configured semantic version."""
  text = path.read_text(encoding="utf-8")
  version_match = VERSION_RE.search(text)
  prerelease_match = PRERELEASE_RE.search(text)
  if version_match is None or prerelease_match is None:
    raise ValueError(f"{path}: version variables are missing")
  prerelease = prerelease_match.group(1)
  return version_match.group(1) + (f"-{prerelease}" if prerelease else "")


def aggregate_membership(repo_root: Path) -> dict[str, list[str]]:
  """Return direct tess-header imports for each stable aggregate."""
  result: dict[str, list[str]] = {}
  for aggregate in AGGREGATES:
    text = (repo_root / aggregate).read_text(encoding="utf-8")
    result[aggregate] = sorted(
        f"include/{header}"
        for header in INCLUDE_RE.findall(text)
        if header.startswith("tess/")
    )
  return result


def current_symbols(repo_root: Path, headers: list[str]) -> set[str]:
  """Extract current public symbols from compatibility headers."""
  symbols: set[str] = set()
  for header in headers:
    source = GENERATED_HEADER_SOURCES.get(header, header)
    symbols.update(
        extract_public_symbols(
            (repo_root / source).read_text(encoding="utf-8")
        )
    )
  return symbols


def snapshot_directories(snapshot_root: Path) -> list[Path]:
  """Return versioned snapshot directories in stable order."""
  if not snapshot_root.is_dir():
    return []
  return sorted(
      path
      for path in snapshot_root.iterdir()
      if path.is_dir() and not path.name.startswith(".")
  )


def release_requires_snapshot(version: str) -> bool:
  """Return whether this source version must carry its own snapshot."""
  match = re.fullmatch(
      r"(?P<major>\d+)\.(?P<minor>\d+)\.(?P<patch>\d+)"
      r"(?:-(?P<prerelease>[0-9A-Za-z.-]+))?",
      version,
  )
  if match is None:
    raise ValueError(f"invalid semantic version: {version}")
  if int(match.group("major")) < 1:
    return False
  prerelease = match.group("prerelease") or ""
  return prerelease == "rc.1" or (
      not prerelease and int(match.group("patch")) == 0
  )


def check_snapshots(
    repo_root: Path,
    snapshot_root: Path,
    header_manifest_path: Path,
    version: str,
) -> list[str]:
  """Return compatibility failures for all snapshots and current sources."""
  failures: list[str] = []
  header_manifest = load_header_manifest(header_manifest_path)
  current_headers = {
      category: set(header_manifest[category])
      for category in ("stable", "optional-stable")
  }
  compatibility_headers = sorted(
      current_headers["stable"] | current_headers["optional-stable"]
  )
  memberships = aggregate_membership(repo_root)
  symbols = current_symbols(repo_root, compatibility_headers)
  directories = snapshot_directories(snapshot_root)

  if release_requires_snapshot(version) and not any(
      directory.name == version for directory in directories
  ):
    failures.append(f"release {version}: compatibility snapshot is missing")

  for directory in directories:
    manifest_path = directory / "manifest.json"
    if not manifest_path.is_file():
      failures.append(f"{directory.name}: manifest.json is missing")
      continue
    payload = json.loads(manifest_path.read_text(encoding="utf-8"))
    if payload.get("version") != directory.name:
      failures.append(
          f"{directory.name}: manifest version does not match directory"
      )

    snapshot_headers = payload.get("headers")
    if not isinstance(snapshot_headers, dict):
      failures.append(f"{directory.name}: headers map is missing")
    else:
      for category in ("stable", "optional-stable"):
        values = snapshot_headers.get(category)
        if not isinstance(values, list) or not all(
            isinstance(value, str) for value in values
        ):
          failures.append(
              f"{directory.name}: headers.{category} must be a string list"
          )
          continue
        for missing in sorted(set(values) - current_headers[category]):
          failures.append(
              f"{directory.name}: {category} header removed or reclassified: "
              f"{missing}"
          )

    snapshot_membership = payload.get("aggregate_membership")
    if not isinstance(snapshot_membership, dict):
      failures.append(f"{directory.name}: aggregate_membership is missing")
    else:
      for aggregate in AGGREGATES:
        values = snapshot_membership.get(aggregate)
        if not isinstance(values, list):
          failures.append(
              f"{directory.name}: membership for {aggregate} is missing"
          )
          continue
        for missing in sorted(set(values) - set(memberships[aggregate])):
          failures.append(
              f"{directory.name}: aggregate member removed from {aggregate}: "
              f"{missing}"
          )

    snapshot_symbols = payload.get("public_symbols")
    if not isinstance(snapshot_symbols, list) or not all(
        isinstance(symbol, str) for symbol in snapshot_symbols
    ):
      failures.append(f"{directory.name}: public_symbols must be a string list")
    else:
      for missing in sorted(set(snapshot_symbols) - symbols):
        failures.append(f"{directory.name}: public symbol removed: {missing}")

    consumer = payload.get("consumer")
    if not isinstance(consumer, str) or not (directory / consumer).is_file():
      failures.append(f"{directory.name}: representative consumer is missing")

    archive_consumer = payload.get("archive_consumer")
    if (
        not isinstance(archive_consumer, str)
        or not (directory / archive_consumer).is_file()
    ):
      failures.append(f"{directory.name}: archive consumer is missing")

    archives = payload.get("archives")
    if not isinstance(archives, list) or not archives:
      failures.append(f"{directory.name}: archive fixtures are missing")
    else:
      for archive in archives:
        if not isinstance(archive, dict):
          failures.append(f"{directory.name}: malformed archive metadata")
          continue
        archive_path = archive.get("path")
        if (
            not isinstance(archive_path, str)
            or not (directory / archive_path).is_file()
            or archive.get("format") != 1
            or archive.get("producer_version") != directory.name
            or not isinstance(archive.get("schema"), str)
            or not archive["schema"]
        ):
          failures.append(
              f"{directory.name}: invalid or missing archive fixture metadata"
          )
  return failures


def main() -> int:
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("--repo-root", type=Path, default=REPO_ROOT)
  parser.add_argument("--snapshots", type=Path, default=DEFAULT_SNAPSHOTS)
  parser.add_argument("--headers", type=Path, default=DEFAULT_HEADERS)
  parser.add_argument("--version-file", type=Path, default=DEFAULT_VERSION)
  args = parser.parse_args()
  failures = check_snapshots(
      args.repo_root,
      args.snapshots,
      args.headers,
      current_version(args.version_file),
  )
  if failures:
    print("\n".join(failures))
    return 1
  print("current sources are a superset of all compatibility snapshots")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
