#!/usr/bin/env python3
"""Validate immutable 1.x compatibility snapshots against current sources."""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
from pathlib import Path, PurePosixPath

from check_public_surface import extract_public_symbols
from header_manifest import (
    GENERATED_HEADER_SOURCES,
    direct_tess_includes,
    load_header_manifest,
)

REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SNAPSHOTS = REPO_ROOT / "compatibility"
DEFAULT_HEADERS = REPO_ROOT / "cmake/tess-headers.json"
DEFAULT_VERSION = REPO_ROOT / "cmake/tess-version.cmake"
AGGREGATES = (
    "include/tess/pathfinding.h",
    "include/tess/simulation.h",
    "include/tess/tess.h",
)
VERSION_RE = re.compile(
    r'^set\(TESS_VERSION\s+"?([^"\s)]+)"?\)', re.MULTILINE
)
PRERELEASE_RE = re.compile(
    r'^set\(TESS_VERSION_PRERELEASE "([^"]*)"\)', re.MULTILINE
)
TARGET_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_-]*$")
PATH_COMPONENT_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._+-]*$")
WINDOWS_RESERVED_NAMES = {
    "AUX",
    "CON",
    "NUL",
    "PRN",
    *(f"COM{index}" for index in range(1, 10)),
    *(f"LPT{index}" for index in range(1, 10)),
}


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
    headers, nonliteral = direct_tess_includes(
        text, aggregate, unconditional_only=True
    )
    if nonliteral:
      headers.append("@nonliteral-include@")
    result[aggregate] = sorted(headers)
  return result


def current_symbols(
    repo_root: Path, headers: list[str]
) -> dict[str, list[str]]:
  """Return documented names keyed by their compatibility header."""
  return {
      header: sorted(
          extract_public_symbols(
              (
                  repo_root / GENERATED_HEADER_SOURCES.get(header, header)
              ).read_text(encoding="utf-8")
          )
      )
      for header in headers
  }


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


def _snapshot_path(
    directory: Path, value: object, path_kind: str
) -> tuple[Path | None, str]:
  if not isinstance(value, str) or not value:
    return None, "missing"
  posix = PurePosixPath(value)
  if (
      posix.is_absolute()
      or posix.as_posix() != value
      or ".." in posix.parts
      or any(
          PATH_COMPONENT_RE.fullmatch(part) is None
          or part.endswith((".", " "))
          or part.split(".", 1)[0].upper() in WINDOWS_RESERVED_NAMES
          for part in posix.parts
      )
  ):
    return None, "unsafe"
  try:
    root = directory.resolve(strict=True)
    resolved = (directory / value).resolve(strict=True)
    resolved.relative_to(root)
  except (OSError, RuntimeError):
    return None, "missing"
  except ValueError:
    return None, "unsafe"
  exists = resolved.is_file() if path_kind == "file" else resolved.is_dir()
  return (resolved, "ok") if exists else (None, "missing")


def _cmake_source_path(project: Path, source: Path) -> str | None:
  try:
    relative = Path(os.path.relpath(source, project)).as_posix()
  except (OSError, ValueError):
    return None
  if re.fullmatch(r"[A-Za-z0-9_./+-]+", relative) is None:
    return None
  return relative


def _consumer_project_is_valid(
    project: Path,
    consumer: Path,
    consumer_target: object,
    archive_consumer: Path,
    archive_target: object,
) -> bool:
  """Require the canonical generated CMake project for immutable fixtures."""
  if (
      not isinstance(consumer_target, str)
      or TARGET_RE.fullmatch(consumer_target) is None
      or not isinstance(archive_target, str)
      or TARGET_RE.fullmatch(archive_target) is None
      or consumer_target == archive_target
      or consumer == archive_consumer
  ):
    return False
  consumer_source = _cmake_source_path(project, consumer)
  archive_source = _cmake_source_path(project, archive_consumer)
  if consumer_source is None or archive_source is None:
    return False
  expected = f"""cmake_minimum_required(VERSION 3.25)
project(tess_compatibility_consumer LANGUAGES CXX)
find_package(tess CONFIG REQUIRED)
add_executable({consumer_target} {consumer_source})
target_link_libraries({consumer_target} PRIVATE tess::tess)
add_executable({archive_target} {archive_source})
target_link_libraries({archive_target} PRIVATE tess::tess)
enable_testing()
add_test(NAME {consumer_target} COMMAND {consumer_target})
add_test(NAME {archive_target}
  COMMAND {archive_target} "${{TESS_SNAPSHOT_DIR}}"
)
"""
  cmakelists = project / "CMakeLists.txt"
  try:
    actual = cmakelists.read_text(encoding="utf-8")
  except OSError:
    return False
  return actual == expected


def _string_list(value: object) -> bool:
  return isinstance(value, list) and all(
      isinstance(item, str) for item in value
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
    current_snapshot = (
        release_requires_snapshot(version) and directory.name == version
    )
    manifest_path = directory / "manifest.json"
    if not manifest_path.is_file():
      failures.append(f"{directory.name}: manifest.json is missing")
      continue
    try:
      payload = json.loads(manifest_path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
      failures.append(f"{directory.name}: manifest.json is invalid: {exc}")
      continue
    if not isinstance(payload, dict):
      failures.append(f"{directory.name}: manifest must be an object")
      continue
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
        if not _string_list(values):
          failures.append(
              f"{directory.name}: headers.{category} must be a string list"
          )
          continue
        if current_snapshot and set(values) != current_headers[category]:
          failures.append(
              f"{directory.name}: current {category} header inventory "
              "does not match the snapshot"
          )
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
        if not _string_list(values):
          failures.append(
              f"{directory.name}: membership for {aggregate} is missing"
          )
          continue
        if current_snapshot and set(values) != set(memberships[aggregate]):
          failures.append(
              f"{directory.name}: current aggregate membership for "
              f"{aggregate} does not match the snapshot"
          )
        for missing in sorted(set(values) - set(memberships[aggregate])):
          failures.append(
              f"{directory.name}: aggregate member removed from {aggregate}: "
              f"{missing}"
          )

    snapshot_symbols = payload.get("public_symbols")
    if not isinstance(snapshot_symbols, dict):
      failures.append(f"{directory.name}: public_symbols must be a map")
    else:
      if current_snapshot and snapshot_symbols != symbols:
        failures.append(
            f"{directory.name}: current public symbol inventory does not "
            "match the snapshot"
        )
      snapshot_compatibility_headers = set()
      if isinstance(snapshot_headers, dict):
        for category in ("stable", "optional-stable"):
          values = snapshot_headers.get(category)
          if _string_list(values):
            snapshot_compatibility_headers.update(values)
      for header in sorted(snapshot_compatibility_headers):
        values = snapshot_symbols.get(header)
        if not _string_list(values):
          failures.append(
              f"{directory.name}: public symbols for {header} are missing"
          )
          continue
        current = set(symbols.get(header, []))
        for missing in sorted(set(values) - current):
          failures.append(
              f"{directory.name}: public symbol removed from {header}: "
              f"{missing}"
          )

    consumer, consumer_status = _snapshot_path(
        directory, payload.get("consumer"), "file"
    )
    archive_consumer, archive_status = _snapshot_path(
        directory, payload.get("archive_consumer"), "file"
    )
    project, project_status = _snapshot_path(
        directory, payload.get("consumer_project"), "directory"
    )
    for label, path, status in (
        ("consumer", consumer, consumer_status),
        ("archive consumer", archive_consumer, archive_status),
        ("consumer project", project, project_status),
    ):
      if status == "unsafe":
        failures.append(
            f"{directory.name}: {label} path must stay inside the snapshot "
            "directory"
        )
      elif path is None:
        failures.append(f"{directory.name}: {label} is missing")
    if (
        project is not None
        and consumer is not None
        and archive_consumer is not None
        and not _consumer_project_is_valid(
            project,
            consumer,
            payload.get("consumer_target"),
            archive_consumer,
            payload.get("archive_consumer_target"),
        )
    ):
      failures.append(
          f"{directory.name}: consumer project must discover tess CONFIG, "
          "link tess::tess, and test both recorded consumers"
      )

    archives = payload.get("archives")
    if not isinstance(archives, list) or not archives:
      failures.append(f"{directory.name}: archive fixtures are missing")
    else:
      for archive in archives:
        if not isinstance(archive, dict):
          failures.append(f"{directory.name}: malformed archive metadata")
          continue
        archive_path, archive_path_status = _snapshot_path(
            directory, archive.get("path"), "file"
        )
        if archive_path_status == "unsafe":
          failures.append(
              f"{directory.name}: archive path must stay inside the snapshot "
              "directory"
          )
          continue
        archive_format = archive.get("format")
        if (
            archive_path is None
            or not isinstance(archive_format, int)
            or isinstance(archive_format, bool)
            or archive_format != 1
            or archive.get("producer_version") != directory.name
            or not isinstance(archive.get("schema"), str)
            or not archive["schema"]
        ):
          failures.append(
              f"{directory.name}: invalid or missing archive fixture metadata"
          )
  return failures


def _git(
    repo_root: Path, arguments: list[str]
) -> subprocess.CompletedProcess[str]:
  return subprocess.run(
      ["git", *arguments],
      cwd=repo_root,
      check=False,
      capture_output=True,
      text=True,
  )


def check_snapshot_immutability(
    repo_root: Path,
    snapshot_root: Path,
    version: str,
) -> list[str]:
  """Verify every earlier snapshot byte-for-byte against its release tag."""
  failures: list[str] = []
  try:
    relative_root = snapshot_root.resolve().relative_to(repo_root.resolve())
  except ValueError:
    return ["snapshot root must be inside the repository"]
  head_exists = _git(
      repo_root, ["rev-parse", "--verify", "--quiet", "HEAD"]
  ).returncode == 0
  tags = (
      _git(repo_root, ["tag", "--merged", "HEAD", "--list", "v1.*"])
      if head_exists
      else subprocess.CompletedProcess([], 0, "", "")
  )
  if tags.returncode != 0:
    return ["release tags could not be enumerated"]
  required_versions = {
      tag[1:]
      for tag in tags.stdout.splitlines()
      if re.fullmatch(r"v1\.\d+\.0(?:-rc\.1)?", tag)
  }
  present = {
      directory.name: directory
      for directory in snapshot_directories(snapshot_root)
  }
  for missing in sorted(required_versions - set(present)):
    failures.append(f"{missing}: released snapshot directory is missing")
  for name in sorted(present):
    tag = f"v{name}"
    tag_exists = _git(
        repo_root, ["rev-parse", "--verify", "--quiet", f"refs/tags/{tag}"]
    ).returncode == 0
    if not tag_exists:
      if name != version:
        failures.append(f"{name}: released snapshot tag {tag} is missing")
      continue
    relative_directory = relative_root / name
    changed = _git(
        repo_root,
        ["diff", "--quiet", tag, "--", relative_directory.as_posix()],
    ).returncode
    if changed != 0:
      failures.append(f"{name}: released snapshot differs from tag {tag}")
  return failures


def main() -> int:
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("--repo-root", type=Path, default=REPO_ROOT)
  parser.add_argument("--snapshots", type=Path, default=DEFAULT_SNAPSHOTS)
  parser.add_argument("--headers", type=Path, default=DEFAULT_HEADERS)
  parser.add_argument("--version-file", type=Path, default=DEFAULT_VERSION)
  args = parser.parse_args()
  version = current_version(args.version_file)
  failures = check_snapshots(
      args.repo_root,
      args.snapshots,
      args.headers,
      version,
  )
  failures.extend(
      check_snapshot_immutability(args.repo_root, args.snapshots, version)
  )
  if failures:
    print("\n".join(failures))
    return 1
  print("current sources are a superset of all compatibility snapshots")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
