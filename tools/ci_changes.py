#!/usr/bin/env python3
"""Classify a Git revision range for the documentation-only CI fast path."""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from collections.abc import Callable, Iterable, Sequence
from dataclasses import dataclass


REVISION_RE = re.compile(r"(?:[0-9a-fA-F]{40}|[0-9a-fA-F]{64})")


@dataclass(frozen=True)
class Classification:
  """A fail-closed CI classification and its human-readable reason."""

  code_required: bool
  reason: str


def is_documentation_path(path: str) -> bool:
  """Return whether a path is covered by documentation-specific checks."""
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


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("base", help="full base commit object ID")
  parser.add_argument("head", help="full head commit object ID")
  return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
  args = parse_args(sys.argv[1:] if argv is None else argv)
  classification = classify_range(args.base, args.head)
  required = str(classification.code_required).lower()
  print(f"code_required={required}")
  print(
    f"CI change classification: {classification.reason}",
    file=sys.stderr,
  )
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
