#!/usr/bin/env python3
"""Validate immutable 1.x compatibility snapshots against current sources."""

from __future__ import annotations

import argparse
import json
import posixpath
import re
import subprocess
from pathlib import Path, PurePosixPath, PureWindowsPath

from api_contract import (
    _tokens,
    callable_name,
    current_api_contract,
    qualified_declares_type_name,
    qualified_may_declare_type_name,
)
from check_public_surface import extract_public_symbols, strip_comments
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
INCLUDE_RE = re.compile(
    r'^\s*#\s*include\s*(?:<([^>\n]+)>|"([^"\n]+)")'
)
CONDITIONAL_RE = re.compile(
    r"^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b"
)
CONTRACT_SCOPE_RE = re.compile(
    r"^(?:type|data-member|aggregate-break) (.+?)(?<!:):(?!:)"
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
    headers: list[str] = []
    conditional_depth = 0
    in_block_comment = False
    for raw_line in text.splitlines():
      line, in_block_comment = strip_comments(
          raw_line, in_block_comment
      )
      directive = CONDITIONAL_RE.match(line)
      if directive is not None:
        kind = directive.group(1)
        if kind in {"if", "ifdef", "ifndef"}:
          conditional_depth += 1
        elif kind == "endif" and conditional_depth:
          conditional_depth -= 1
        continue
      include = INCLUDE_RE.match(line)
      imported = next(
          (group for group in include.groups() if group is not None), None
      ) if include is not None else None
      if (
          conditional_depth == 0
          and imported is not None
      ):
        if include.group(1) is not None or imported.startswith("tess/"):
          headers.append(f"include/{imported}")
        else:
          headers.append(
              posixpath.normpath(
                  (Path(aggregate).parent / imported).as_posix()
              )
          )
    result[aggregate] = sorted(headers)
  return result


def _contract_scope(contract: str) -> str | None:
  match = CONTRACT_SCOPE_RE.match(contract)
  return match.group(1) if match is not None else None


def _aggregate_scope(contract: str) -> str | None:
  match = re.match(r"^aggregate (.+?)(?:(?<!:):(?!:)|$)", contract)
  return match.group(1) if match is not None else None


def _callable_identity(contract: str) -> str | None:
  match = re.match(
      r"^(?:member|function) (.+?)(?<!:):(?!:)(.*)$", contract
  )
  if match is None:
    return None
  scope, declaration = match.groups()
  name = callable_name(declaration)
  if name is None:
    return None
  return f"{scope}::{name}"


def _using_callable_identity(contract: str) -> str | None:
  identities = _using_callable_identities(contract)
  return next(iter(identities)) if len(identities) == 1 else None


def _using_callable_identities(contract: str) -> set[str]:
  match = re.match(
      r"^(?:member|declaration) (.+?)(?<!:):(?!:)(.*)$", contract
  )
  if match is None:
    return set()
  scope, declaration = match.groups()
  tokens = _tokens(declaration)
  if "using" not in tokens or "::" not in tokens:
    return set()
  declarators = _using_declarators(tokens[tokens.index("using") + 1 :])
  return {
      identity
      for declarator in declarators
      for identity in _using_declarator_identities(scope, declarator)
  }


def _using_declarators(tokens: list[str]) -> list[list[str]]:
  """Split a using-declarator-list without splitting nested commas."""
  result: list[list[str]] = []
  current: list[str] = []
  round_depth = square_depth = brace_depth = angle_depth = 0
  for token in tokens:
    if token == "," and not (round_depth or square_depth or brace_depth):
      separator = len(current) - 1 - current[::-1].index("::") \
          if "::" in current else -1
      suffix = current[separator + 1 :]
      complete = (
          len(suffix) == 1 and re.fullmatch(r"[A-Za-z_]\w*", suffix[0])
      ) or (suffix and suffix[0] == "operator" and len(suffix) > 1)
      if (angle_depth == 0 or complete) and suffix != ["operator"]:
        result.append(current)
        current = []
        continue
    current.append(token)
    if token == "(":
      round_depth += 1
    elif token == ")" and round_depth:
      round_depth -= 1
    elif token in {"[", "[["}:
      square_depth += 1
    elif token in {"]", "]]"} and square_depth:
      square_depth -= 1
    elif token == "{":
      brace_depth += 1
    elif token == "}" and brace_depth:
      brace_depth -= 1
    elif not (round_depth or square_depth or brace_depth):
      separator = len(current) - 1 - current[::-1].index("::") \
          if "::" in current else -1
      operator_name = "operator" in current[separator + 1 :]
      if token == "<" and not operator_name:
        angle_depth += 1
      elif token == ">" and angle_depth and not operator_name:
        angle_depth -= 1
      elif token == ">>" and angle_depth and not operator_name:
        angle_depth = max(0, angle_depth - 2)
  if current:
    result.append(current)
  return result


def _using_declarator_identities(
    scope: str, tokens: list[str]
) -> set[str]:
  if "::" not in tokens:
    return set()
  position = len(tokens) - 1 - tokens[::-1].index("::")
  if "=" in tokens[:position]:
    return set()
  suffix = tokens[position + 1 :]
  if suffix and suffix[0] == "operator":
    name = "operator" + "".join(suffix[1:])
    return {f"{scope}::{name}"} if name != "operator" else set()
  name = next(
      (
          token
          for token in suffix
          if re.fullmatch(r"[A-Za-z_]\w*", token)
      ),
      None,
  )
  if name is None:
    return set()
  owner = scope.rsplit("::", 1)[-1]
  qualifier = tokens[:position]
  ordinary = f"{scope}::{name}"
  if qualified_declares_type_name(qualifier, name):
    return {f"{scope}::{owner}"}
  if qualified_may_declare_type_name(qualifier, name):
    return {ordinary, f"{scope}::{owner}"}
  return {ordinary}


def _inherited_callable_identities(declarations: set[str]) -> set[str]:
  """Return callable identities visible through snapshotted base classes."""
  types = {
      scope: declaration
      for contract in declarations
      if (match := re.match(
          r"^type (.+?)(?<!:):(?!:)(.*)$", contract
      )) is not None
      for scope, declaration in [match.groups()]
  }
  own: dict[str, set[str]] = {scope: set() for scope in types}
  for contract in declarations:
    for identity in (
        _callable_identity(contract),
        *_using_callable_identities(contract),
    ):
      if identity is not None:
        scope, name = identity.rsplit("::", 1)
        own.setdefault(scope, set()).add(name)

  by_name: dict[str, set[str]] = {}
  for scope in types:
    by_name.setdefault(scope.rsplit("::", 1)[-1], set()).add(scope)
  aliases: dict[str, set[str]] = {}
  for contract in declarations:
    match = re.match(
        r"^declaration (.+?)(?<!:):(?!:)using ([A-Za-z_]\w*) = (.*)$",
        contract,
    )
    if match is not None:
      namespace, alias, target = match.groups()
      aliases.setdefault(alias, set()).update(
          name for name in by_name if re.search(rf"\b{re.escape(name)}\b", target)
      )
  bases: dict[str, set[str]] = {}
  for scope, declaration in types.items():
    tokens = _tokens(declaration)
    if ":" not in tokens:
      continue
    candidates = _base_head_names(tokens[tokens.index(":") + 1 :])
    candidates.update(
        token for token in tokens[tokens.index(":") + 1 :] if token in by_name
    )
    candidates.update(
        target for candidate in tuple(candidates) for target in aliases.get(candidate, set())
    )
    bases[scope] = {
        base
        for name in candidates
        for base in by_name.get(name, set())
        if base != scope
    }

  visible = {scope: set(names) for scope, names in own.items()}
  changed = True
  while changed:
    changed = False
    for scope, direct_bases in bases.items():
      inherited = set().union(
          *(visible.get(base, set()) for base in direct_bases)
      ) if direct_bases else set()
      before = len(visible.setdefault(scope, set()))
      visible[scope].update(inherited)
      changed |= len(visible[scope]) != before
  return {
      f"{scope}::{name}"
      for scope, direct_bases in bases.items()
      if direct_bases
      for name in visible[scope]
      if name not in own.get(scope, set())
  }


def _base_head_names(tokens: list[str]) -> set[str]:
  """Return terminal names of ordinary base-specifier heads."""
  names: set[str] = set()
  head: list[str] = []
  angle_depth = round_depth = square_depth = 0
  for token in [*tokens, ","]:
    if token == "," and not (angle_depth or round_depth or square_depth):
      identifiers = [
          item
          for item in head
          if re.fullmatch(r"[A-Za-z_]\w*", item)
          and item not in {"public", "protected", "private", "virtual"}
      ]
      if identifiers:
        names.add(identifiers[-1])
      head = []
      continue
    if not angle_depth:
      head.append(token)
    if token == "(":
      round_depth += 1
    elif token == ")" and round_depth:
      round_depth -= 1
    elif token in {"[", "[["}:
      square_depth += 1
    elif token in {"]", "]]"} and square_depth:
      square_depth -= 1
    elif not (round_depth or square_depth):
      if token == "<":
        angle_depth += 1
      elif token == ">" and angle_depth:
        angle_depth -= 1
      elif token == ">>" and angle_depth:
        angle_depth = max(0, angle_depth - 2)
  return names


def _macro_identity(contract: str, category: str = "macro") -> str | None:
  match = re.match(
      rf"^{re.escape(category)} (.+?)(?<!:):(?!:)", contract
  )
  if match is None:
    return None
  declaration = match.group(1)
  return declaration.rsplit("::", 1)[-1]


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
  if (
      not isinstance(consumer_target, str)
      or TARGET_RE.fullmatch(consumer_target) is None
      or not isinstance(archive_target, str)
      or TARGET_RE.fullmatch(archive_target) is None
  ):
    return False
  commands = _cmake_commands(
      (project / "CMakeLists.txt").read_text(encoding="utf-8")
  )
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
        args and args[0] == target and "tess::tess" in args[1:]
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
      if needs_snapshot and not any(
          "TESS_SNAPSHOT_DIR" in argument for argument in args
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
  api_contract = current_api_contract(repo_root, compatibility_headers)
  directories = snapshot_directories(snapshot_root)

  if release_requires_snapshot(version) and not any(
      directory.name == version for directory in directories
  ):
    failures.append(f"release {version}: compatibility snapshot is missing")

  for directory in directories:
    is_current_snapshot = (
        release_requires_snapshot(version) and directory.name == version
    )
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
        if is_current_snapshot and set(values) != current_headers[category]:
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
        if not isinstance(values, list):
          failures.append(
              f"{directory.name}: membership for {aggregate} is missing"
          )
          continue
        if is_current_snapshot and set(values) != set(
            memberships[aggregate]
        ):
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
    if not isinstance(snapshot_symbols, list) or not all(
        isinstance(symbol, str) for symbol in snapshot_symbols
    ):
      failures.append(f"{directory.name}: public_symbols must be a string list")
    else:
      if is_current_snapshot and set(snapshot_symbols) != symbols:
        failures.append(
            f"{directory.name}: current public symbol inventory does not "
            "match the snapshot"
        )
      for missing in sorted(set(snapshot_symbols) - symbols):
        failures.append(f"{directory.name}: public symbol removed: {missing}")

    snapshot_contract = payload.get("api_contract")
    if not isinstance(snapshot_contract, dict):
      failures.append(f"{directory.name}: api_contract map is missing")
    else:
      all_snapshot_declarations = {
          declaration
          for declarations in snapshot_contract.values()
          if isinstance(declarations, list)
          for declaration in declarations
          if isinstance(declaration, str)
      }
      inherited_callables = _inherited_callable_identities(
          all_snapshot_declarations
      )
      valid_snapshot_contract: dict[str, set[str]] = {}
      snapshot_header_values: list[str] = []
      if isinstance(snapshot_headers, dict):
        for category in ("stable", "optional-stable"):
          values = snapshot_headers.get(category)
          if isinstance(values, list):
            snapshot_header_values.extend(
                value for value in values if isinstance(value, str)
            )
      for header in sorted(set(snapshot_header_values)):
        declarations = snapshot_contract.get(header)
        if not isinstance(declarations, list) or not all(
            isinstance(declaration, str) for declaration in declarations
        ):
          failures.append(
              f"{directory.name}: api_contract for {header} is missing"
          )
          continue
        valid_snapshot_contract[header] = set(declarations)
        current_declarations = set(api_contract.get(header, []))
        snapshot_declarations = set(declarations)
        for missing in sorted(snapshot_declarations - current_declarations):
          if missing.startswith("aggregate "):
            failures.append(
                f"{directory.name}: aggregate compatibility changed: "
                f"{header}: {missing}"
            )
          else:
            failures.append(
                f"{directory.name}: API declaration changed or removed: "
                f"{header}: {missing}"
            )
        snapshot_types = {
            scope
            for declaration in snapshot_declarations
            if declaration.startswith("type ")
            if (scope := _contract_scope(declaration)) is not None
        }
        snapshot_aggregates = {
            scope
            for declaration in snapshot_declarations
            if (scope := _aggregate_scope(declaration)) is not None
        }
        snapshot_callables = {
            identity
            for declaration in snapshot_declarations
            for identity in (
                _callable_identity(declaration),
                *_using_callable_identities(declaration),
            )
            if identity is not None
        } | inherited_callables
        snapshot_macros = {
            identity
            for declaration in snapshot_declarations
            if (identity := _macro_identity(declaration)) is not None
        }
        for addition in sorted(
            current_declarations - snapshot_declarations
        ):
          owner = _contract_scope(addition)
          if addition.startswith("data-member ") and owner in snapshot_types:
            failures.append(
                f"{directory.name}: public data member added to existing "
                f"type: {header}: {addition}"
            )
          if (
              addition.startswith("aggregate-break ")
              and owner in snapshot_aggregates
          ):
            failures.append(
                f"{directory.name}: aggregate compatibility changed: "
                f"{header}: {addition}"
            )
          identities = {
              identity
              for identity in (
                  _callable_identity(addition),
                  *_using_callable_identities(addition),
              )
              if identity is not None
          }
          if identities & snapshot_callables:
            failures.append(
                f"{directory.name}: overload added to existing callable: "
                f"{header}: {addition}"
            )
          undefined = _macro_identity(addition, "macro-undef")
          if undefined is not None and undefined in snapshot_macros:
            failures.append(
                f"{directory.name}: stable macro undefined: "
                f"{header}: {addition}"
            )
          redefined = _macro_identity(addition)
          if redefined is not None and redefined in snapshot_macros:
            failures.append(
                f"{directory.name}: stable macro redefined: "
                f"{header}: {addition}"
            )
      if is_current_snapshot:
        current_contract = {
            header: set(api_contract.get(header, []))
            for header in compatibility_headers
        }
        if valid_snapshot_contract != current_contract:
          failures.append(
              f"{directory.name}: current API contract does not match "
              "the snapshot"
          )

    consumer, consumer_status = _snapshot_path(
        directory, payload.get("consumer"), "file"
    )
    if consumer_status == "unsafe":
      failures.append(
          f"{directory.name}: consumer path must stay inside the "
          "snapshot directory"
      )
    elif consumer is None:
      failures.append(f"{directory.name}: representative consumer is missing")

    archive_consumer, archive_consumer_status = _snapshot_path(
        directory, payload.get("archive_consumer"), "file"
    )
    if archive_consumer_status == "unsafe":
      failures.append(
          f"{directory.name}: archive consumer path must stay inside the "
          "snapshot directory"
      )
    elif archive_consumer is None:
      failures.append(f"{directory.name}: archive consumer is missing")

    consumer_project, project_status = _snapshot_path(
        directory, payload.get("consumer_project"), "directory"
    )
    project_file = (
        consumer_project / "CMakeLists.txt"
        if consumer_project is not None
        else None
    )
    if project_status == "unsafe":
      failures.append(
          f"{directory.name}: consumer project path must stay inside the "
          "snapshot directory"
      )
    elif project_file is None or not project_file.is_file():
      failures.append(f"{directory.name}: consumer project is missing")
    elif consumer is not None and archive_consumer is not None:
      if not _consumer_project_is_valid(
          consumer_project,
          consumer,
          payload.get("consumer_target"),
          archive_consumer,
          payload.get("archive_consumer_target"),
      ):
        failures.append(
            f"{directory.name}: consumer project must discover tess CONFIG "
            "and link tess::tess, build both recorded consumers, and test "
            "them"
        )

    archives = payload.get("archives")
    if not isinstance(archives, list) or not archives:
      failures.append(f"{directory.name}: archive fixtures are missing")
    else:
      for archive in archives:
        if not isinstance(archive, dict):
          failures.append(f"{directory.name}: malformed archive metadata")
          continue
        archive_path, archive_status = _snapshot_path(
            directory, archive.get("path"), "file"
        )
        if archive_status == "unsafe":
          failures.append(
              f"{directory.name}: archive path must stay inside the "
              "snapshot directory"
          )
          continue
        if (
            archive_status != "ok"
            or archive_path is None
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

  for name, directory in sorted(present.items()):
    tag = f"v{name}"
    tag_exists = _git(
        repo_root, ["rev-parse", "--verify", "--quiet", f"refs/tags/{tag}"]
    ).returncode == 0
    if not tag_exists:
      if name != version:
        failures.append(
            f"{name}: released snapshot tag {tag} is missing"
        )
      continue
    relative_directory = relative_root / name
    changed = _git(
        repo_root,
        ["diff", "--quiet", tag, "--", relative_directory.as_posix()],
    ).returncode
    if changed != 0:
      failures.append(
          f"{name}: released snapshot differs from tag {tag}"
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
  failures.extend(
      check_snapshot_immutability(
          args.repo_root,
          args.snapshots,
          current_version(args.version_file),
      )
  )
  if failures:
    print("\n".join(failures))
    return 1
  print("current sources are a superset of all compatibility snapshots")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
