"""Shared reader and validators for tess's installed-header classes."""

from __future__ import annotations

import json
import posixpath
import re
from pathlib import Path

HEADER_CLASSES = (
    "stable",
    "optional-stable",
    "experimental",
    "implementation-only",
)
GENERATED_HEADER_SOURCES = {
    "include/tess/version.h": "include/tess/version.h.in",
}
_INCLUDE_RE = re.compile(
    r'^\s*#\s*include\s*(?:<([^>\n]+)>|"([^"\n]+)")'
)
_INCLUDE_DIRECTIVE_RE = re.compile(r"^\s*#\s*include\b")
_CONDITIONAL_RE = re.compile(
    r"^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b"
)


def direct_tess_includes(
    text: str, aggregate: str, *, unconditional_only: bool = False
) -> tuple[list[str], bool]:
  """Return canonical literal tess includes and whether one was nonliteral."""
  text = re.sub(r"\\\r?\n[ \t]*", " ", text)
  paths: list[str] = []
  conditional_depth = 0
  in_block_comment = False
  nonliteral = False
  for raw_line in text.splitlines():
    line, in_block_comment = _strip_comments(raw_line, in_block_comment)
    directive = _CONDITIONAL_RE.match(line)
    if directive is not None:
      kind = directive.group(1)
      if kind in {"if", "ifdef", "ifndef"}:
        conditional_depth += 1
      elif kind == "endif" and conditional_depth:
        conditional_depth -= 1
      continue
    if unconditional_only and conditional_depth:
      continue
    if not _INCLUDE_DIRECTIVE_RE.match(line):
      continue
    include = _INCLUDE_RE.match(line)
    if include is None:
      nonliteral = True
      continue
    angle, quote = include.groups()
    imported = angle or quote
    if angle is not None or imported.startswith("tess/"):
      path = posixpath.normpath(f"include/{imported}")
    else:
      path = posixpath.normpath(
          (Path(aggregate).parent / imported).as_posix()
      )
    if path.startswith("include/tess/"):
      paths.append(path)
  return sorted(paths), nonliteral


def _strip_comments(line: str, in_block: bool) -> tuple[str, bool]:
  out: list[str] = []
  index = 0
  while index < len(line):
    if in_block:
      end = line.find("*/", index)
      if end < 0:
        return "".join(out), True
      index = end + 2
      in_block = False
    elif line.startswith("//", index):
      break
    elif line.startswith("/*", index):
      in_block = True
      index += 2
    else:
      out.append(line[index])
      index += 1
  return "".join(out), in_block


def load_header_manifest(path: Path) -> dict[str, list[str]]:
  """Load the four exhaustive header classes from ``path``."""
  payload = json.loads(path.read_text(encoding="utf-8"))
  if not isinstance(payload, dict) or tuple(payload) != HEADER_CLASSES:
    raise ValueError(
        "header manifest must contain the four ordered classes: "
        + ", ".join(HEADER_CLASSES)
    )
  for category, headers in payload.items():
    if not isinstance(headers, list) or not all(
        isinstance(header, str) for header in headers
    ):
      raise ValueError(f"header class {category!r} must be a string list")
  return payload


def headers_in_classes(
    manifest: dict[str, list[str]], *categories: str
) -> list[str]:
  """Return manifest headers in class and declaration order."""
  return [header for category in categories for header in manifest[category]]
