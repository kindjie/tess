#!/usr/bin/env python3
"""Validate immutable 1.x compatibility snapshots against current sources."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
from pathlib import Path, PurePosixPath, PureWindowsPath

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


def current_symbols(repo_root: Path, headers: list[str]) -> set[str]:
  """Return the documented name inventory for compatibility headers."""
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


def _snapshot_path(
    directory: Path, value: object, path_kind: str
) -> tuple[Path | None, str]:
  if not isinstance(value, str) or not value:
    return None, "missing"
  posix = PurePosixPath(value)
  windows = PureWindowsPath(value)
  if (
      posix.is_absolute()
      or windows.is_absolute()
      or ".." in posix.parts
      or ".." in windows.parts
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


def _cmake_without_comments(text: str) -> str:
  result: list[str] = []
  index = 0
  quoted = False
  escaped = False
  bracket_end: str | None = None
  while index < len(text):
    if bracket_end is not None:
      if text.startswith(bracket_end, index):
        index += len(bracket_end)
        bracket_end = None
      else:
        if text[index] == "\n":
          result.append("\n")
        index += 1
      continue
    character = text[index]
    if character == '"' and not escaped:
      quoted = not quoted
    if character == "#" and not quoted:
      bracket = re.match(r"#\[(=*)\[", text[index:])
      if bracket is not None:
        bracket_end = f"]{bracket.group(1)}]"
        index += len(bracket.group(0))
        continue
      newline = text.find("\n", index)
      if newline == -1:
        break
      result.append("\n")
      index = newline + 1
      escaped = False
      continue
    result.append(character)
    escaped = character == "\\" and not escaped
    if character != "\\":
      escaped = False
    index += 1
  return "".join(result)


def _cmake_commands(text: str) -> list[tuple[str, list[str]]]:
  clean = _cmake_without_comments(text)
  result: list[tuple[str, list[str]]] = []
  position = 0
  while position < len(clean):
    if clean[position] == '"':
      position += 1
      while position < len(clean):
        if clean[position] == '"' and clean[position - 1] != "\\":
          position += 1
          break
        position += 1
      continue
    bracket = re.match(r"\[(=*)\[", clean[position:])
    if bracket is not None:
      closing = f"]{bracket.group(1)}]"
      end = clean.find(closing, position + len(bracket.group(0)))
      position = len(clean) if end == -1 else end + len(closing)
      continue
    match = re.match(r"([A-Za-z_]\w*)", clean[position:])
    if match is None:
      position += 1
      continue
    name = match.group(1)
    opening = position + len(name)
    while opening < len(clean) and clean[opening].isspace():
      opening += 1
    if opening >= len(clean) or clean[opening] != "(":
      position = opening
      continue
    index = opening + 1
    depth = 1
    quoted = False
    escaped = False
    bracket_end: str | None = None
    while index < len(clean) and depth:
      if bracket_end is not None:
        if clean.startswith(bracket_end, index):
          index += len(bracket_end)
          bracket_end = None
        else:
          index += 1
        continue
      character = clean[index]
      if character == '"' and not escaped:
        quoted = not quoted
      elif not quoted:
        bracket = re.match(r"\[(=*)\[", clean[index:])
        if bracket is not None:
          bracket_end = f"]{bracket.group(1)}]"
          index += len(bracket.group(0))
          continue
        if character == "(":
          depth += 1
        elif character == ")":
          depth -= 1
      escaped = character == "\\" and not escaped
      if character != "\\":
        escaped = False
      index += 1
    if depth:
      break
    body = clean[opening + 1 : index - 1]
    arguments = [
        token[1:-1] if token.startswith('"') else token
        for token in re.findall(r'"(?:\\.|[^"\\])*"|[^\s()]+', body)
    ]
    result.append((name.lower(), arguments))
    position = index
  return result


def _source_argument_matches(
    argument: str, project: Path, source: Path
) -> bool:
  if argument.startswith("$"):
    return False
  try:
    return (project / argument).resolve() == source
  except (OSError, RuntimeError):
    return False


def _consumer_project_is_valid(
    project: Path,
    consumer: Path,
    consumer_target: object,
    archive_consumer: Path,
    archive_target: object,
) -> bool:
  """Validate the fixed, intentionally control-flow-free CMake fixture."""
  if (
      not isinstance(consumer_target, str)
      or TARGET_RE.fullmatch(consumer_target) is None
      or not isinstance(archive_target, str)
      or TARGET_RE.fullmatch(archive_target) is None
  ):
    return False
  cmakelists = project / "CMakeLists.txt"
  try:
    commands = _cmake_commands(cmakelists.read_text(encoding="utf-8"))
  except OSError:
    return False
  allowed_commands = {
      "add_executable",
      "add_test",
      "cmake_minimum_required",
      "enable_testing",
      "find_package",
      "project",
      "target_link_libraries",
  }
  if not commands or any(name not in allowed_commands for name, _ in commands):
    return False
  finds = [args for name, args in commands if name == "find_package"]
  executables = [
      args for name, args in commands if name == "add_executable"
  ]
  links = [
      args for name, args in commands if name == "target_link_libraries"
  ]
  tests = [args for name, args in commands if name == "add_test"]

  def has_executable(target: str, source: Path) -> bool:
    return any(
        args
        and args[0] == target
        and any(
            _source_argument_matches(argument, project, source)
            for argument in args[1:]
        )
        for args in executables
    )

  def links_tess(target: str) -> bool:
    return any(
        len(args) >= 3
        and args[0] == target
        and args[1] == "PRIVATE"
        and "tess::tess" in args[2:]
        for args in links
    )

  def has_test(target: str, needs_snapshot: bool) -> bool:
    for args in tests:
      if "NAME" not in args or "COMMAND" not in args:
        continue
      name_index = args.index("NAME")
      command_index = args.index("COMMAND")
      if (
          name_index + 1 >= len(args)
          or args[name_index + 1] != target
          or command_index + 1 >= len(args)
      ):
        continue
      if args[command_index + 1] != target:
        continue
      snapshot_index = command_index + 2
      if needs_snapshot and (
          snapshot_index >= len(args)
          or args[snapshot_index] != "${TESS_SNAPSHOT_DIR}"
      ):
        continue
      return True
    return False

  return (
      any(
          args
          and args[0] == "tess"
          and "CONFIG" in args[1:]
          and "REQUIRED" in args[1:]
          for args in finds
      )
      and has_executable(consumer_target, consumer)
      and links_tess(consumer_target)
      and has_test(consumer_target, False)
      and has_executable(archive_target, archive_consumer)
      and links_tess(archive_target)
      and has_test(archive_target, True)
      and any(name == "enable_testing" and not args for name, args in commands)
  )


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
    if not _string_list(snapshot_symbols):
      failures.append(f"{directory.name}: public_symbols must be a string list")
    else:
      if current_snapshot and set(snapshot_symbols) != symbols:
        failures.append(
            f"{directory.name}: current public symbol inventory does not "
            "match the snapshot"
        )
      for missing in sorted(set(snapshot_symbols) - symbols):
        failures.append(f"{directory.name}: public symbol removed: {missing}")

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
        if (
            archive_path is None
            or archive.get("format") != 1
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
