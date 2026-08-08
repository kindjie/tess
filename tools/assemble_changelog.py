#!/usr/bin/env python3
"""Assemble changelog fragments into the maintained changelogs.

Concurrent branches that each edit `CHANGELOG.md` conflict with every
other such branch, so a stack of N pull requests costs O(N^2) conflict
resolutions in a file where a mis-resolution silently drops an entry.
Fragments move each entry into its own file, so branches touch disjoint
paths and merge cleanly; assembly happens once, at release.

Two fragment sets, matching the two maintained changelogs:

  changelog.d/<slug>.<category>.md
      Release-facing entries. `<category>` is one of the Keep a Changelog
      sections. The file holds complete markdown list items, so assembly
      is concatenation rather than reformatting.

  docs/decisions/changelog.d/<YYYY-MM-DD>-<slug>.md
      Design-decision entries. The file holds a complete `## <date> - ...`
      section. Assembly orders by filename, newest first.

Usage:
    assemble_changelog.py --check
    assemble_changelog.py --preview
    assemble_changelog.py --release 0.13.0 --date 2026-09-01
"""

from __future__ import annotations

import argparse
import datetime
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
RELEASE_FRAGMENTS = REPO_ROOT / "changelog.d"
DECISION_FRAGMENTS = REPO_ROOT / "docs" / "decisions" / "changelog.d"
RELEASE_CHANGELOG = REPO_ROOT / "CHANGELOG.md"
DECISION_CHANGELOG = REPO_ROOT / "docs" / "decisions" / "CHANGELOG.md"

# Rendered in this order, which is the order the existing changelog uses.
CATEGORIES = (
  "added",
  "changed",
  "deprecated",
  "removed",
  "fixed",
  "security",
  "performance",
  "documentation",
)

RELEASE_NAME_RE = re.compile(
  rf"^(?P<slug>[a-z0-9][a-z0-9-]*)\.(?P<category>{'|'.join(CATEGORIES)})\.md$"
)
DECISION_NAME_RE = re.compile(
  r"^(?P<date>\d{4}-\d{2}-\d{2})-(?P<slug>[a-z0-9][a-z0-9-]*)\.md$"
)
UNRELEASED_HEADING = "## [Unreleased]"
KEEP_FILE = ".gitkeep"


class FragmentError(Exception):
  """A fragment is named or shaped in a way assembly cannot honor."""


def _fragment_files(directory: Path) -> list[Path]:
  if not directory.is_dir():
    return []
  return sorted(
    path
    for path in directory.iterdir()
    if path.is_file() and path.name != KEEP_FILE
  )


def validate_release_fragment(path: Path) -> str:
  """Return the category for `path`, or raise FragmentError."""
  match = RELEASE_NAME_RE.match(path.name)
  if match is None:
    raise FragmentError(
      f"{path.name}: expected <slug>.<category>.md with category one of "
      f"{', '.join(CATEGORIES)}"
    )
  body = path.read_text(encoding="utf-8")
  if not body.strip():
    raise FragmentError(f"{path.name}: empty fragment")
  seen_item = False
  for line in body.splitlines():
    if not line.strip():
      continue
    # A fragment holds complete list items so assembly stays a
    # concatenation; anything else would need reformatting here and would
    # render differently than it reads in review.
    if line.startswith("- "):
      seen_item = True
      continue
    if line.startswith("  "):
      if not seen_item:
        # Nonempty, and every line "continues" -- but the assembled
        # category would hold no list item at all.
        raise FragmentError(
          f"{path.name}: indented line before any list item ('- ')"
        )
      continue
    raise FragmentError(
      f"{path.name}: every line must start a list item ('- ') or "
      f"continue one already begun (two spaces); found: {line[:40]!r}"
    )
  return match.group("category")


def validate_decision_fragment(path: Path) -> str:
  """Return the date for `path`, or raise FragmentError."""
  match = DECISION_NAME_RE.match(path.name)
  if match is None:
    raise FragmentError(
      f"{path.name}: expected <YYYY-MM-DD>-<slug>.md"
    )
  body = path.read_text(encoding="utf-8")
  stripped = body.strip()
  if not stripped:
    raise FragmentError(f"{path.name}: empty fragment")
  date = match.group("date")
  expected = f"## {date} - "
  if not stripped.startswith(expected):
    raise FragmentError(
      f"{path.name}: must open with {expected!r} so the assembled file "
      f"keeps one heading style and the date matches the filename"
    )
  return date


def check() -> list[str]:
  """Validate every fragment; return the problems found."""
  problems: list[str] = []
  for path in _fragment_files(RELEASE_FRAGMENTS):
    try:
      validate_release_fragment(path)
    except FragmentError as error:
      problems.append(str(error))
  for path in _fragment_files(DECISION_FRAGMENTS):
    try:
      validate_decision_fragment(path)
    except FragmentError as error:
      problems.append(str(error))
  return problems


def parse_sections(body: str) -> dict[str, list[str]]:
  """Split a changelog body into {category: [entry-block, ...]}.

  Entries written before fragments existed still live under
  `## [Unreleased]`. Emitting fragment sections beside them would produce
  two `### Fixed` headings under one release, so a release merges the two
  by category. Once the transition is over this returns nothing and the
  merge is a no-op.
  """
  sections: dict[str, list[str]] = {}
  current: str | None = None
  buffer: list[str] = []

  def flush() -> None:
    if current is not None and buffer:
      text = "\n".join(buffer).strip("\n")
      if text:
        sections.setdefault(current, []).append(text)

  for line in body.splitlines():
    if line.startswith("### "):
      flush()
      buffer = []
      current = line[4:].strip().lower()
      continue
    if current is not None:
      buffer.append(line)
  flush()
  return sections


def render_release_sections(existing: dict[str, list[str]] | None = None) -> str:
  """Render pending release fragments, merged with any existing body."""
  by_category: dict[str, list[str]] = {name: [] for name in CATEGORIES}
  for name, entries in (existing or {}).items():
    by_category.setdefault(name, []).extend(entries)
  for path in _fragment_files(RELEASE_FRAGMENTS):
    category = validate_release_fragment(path)
    by_category[category].append(path.read_text(encoding="utf-8").rstrip("\n"))

  ordered = list(CATEGORIES) + [
    name for name in by_category if name not in CATEGORIES
  ]
  chunks: list[str] = []
  for category in ordered:
    entries = by_category.get(category) or []
    if not entries:
      continue
    chunks.append(f"### {category.capitalize()}\n\n" + "\n".join(entries))
  return "\n\n".join(chunks)


def render_decision_sections() -> str:
  """Render pending decision fragments, newest first."""
  paths = _fragment_files(DECISION_FRAGMENTS)
  for path in paths:
    validate_decision_fragment(path)
  ordered = sorted(paths, key=lambda path: path.name, reverse=True)
  return "\n\n".join(
    path.read_text(encoding="utf-8").strip() for path in ordered
  )


DATED_HEADING_RE = re.compile(r"^## (\d{4}-\d{2}-\d{2}) - ", re.M)


def split_dated_sections(text: str) -> list[tuple[str, str]]:
  """Split a decision changelog body into [(date, section-text), ...]."""
  matches = list(DATED_HEADING_RE.finditer(text))
  sections: list[tuple[str, str]] = []
  for position, match in enumerate(matches):
    start = match.start()
    end = matches[position + 1].start() if position + 1 < len(matches) else len(text)
    sections.append((match.group(1), text[start:end].strip("\n")))
  return sections


def release(version: str, date: str, *, dry_run: bool) -> int:
  """Fold pending fragments into both changelogs under `version`."""
  # Checked before any write or delete: a mistyped date would otherwise
  # land verbatim in the release heading AND consume every fragment,
  # reporting success.
  # Not date.fromisoformat: since 3.11 it also accepts basic forms such
  # as "20260901", which would reach the release heading verbatim.
  try:
    if not re.fullmatch(r"\d{4}-\d{2}-\d{2}", date):
      raise ValueError(date)
    datetime.date.fromisoformat(date)
  except ValueError:
    print(
      f"changelog: --date {date!r} is not a YYYY-MM-DD calendar date",
      file=sys.stderr,
    )
    return 1
  problems = check()
  if problems:
    for problem in problems:
      print(f"changelog: {problem}", file=sys.stderr)
    return 1

  release_body = render_release_sections()
  decision_body = render_decision_sections()
  if not release_body and not decision_body:
    print("changelog: no fragments to assemble", file=sys.stderr)
    return 1

  if release_body:
    text = RELEASE_CHANGELOG.read_text(encoding="utf-8")
    if UNRELEASED_HEADING not in text:
      print(
        f"changelog: {RELEASE_CHANGELOG.name} has no {UNRELEASED_HEADING}",
        file=sys.stderr,
      )
      return 1
    head, _, tail = text.partition(UNRELEASED_HEADING)
    # Everything up to the next release heading is the Unreleased body.
    next_release = tail.find("\n## ")
    if next_release == -1:
      unreleased_body, remainder = tail, ""
    else:
      unreleased_body, remainder = tail[:next_release], tail[next_release + 1 :]
    merged = render_release_sections(parse_sections(unreleased_body))
    section = f"## [{version}] - {date}\n\n{merged}\n"
    new = f"{head}{UNRELEASED_HEADING}\n\n{section}\n{remainder}"
    if dry_run:
      print(section)
    else:
      RELEASE_CHANGELOG.write_text(new, encoding="utf-8")

  if decision_body:
    text = DECISION_CHANGELOG.read_text(encoding="utf-8")
    marker = "\n## "
    index = text.index(marker)
    preamble, existing = text[:index], text[index:]
    # Merge pending fragments with the sections already in the file and
    # sort the union. Prepending would put a backdated fragment above
    # decisions that predate it, since sorting the pending set alone says
    # nothing about where it belongs relative to history.
    sections = split_dated_sections(existing) + split_dated_sections(
      "\n" + decision_body
    )
    sections.sort(key=lambda item: item[0], reverse=True)
    merged = "\n\n".join(body for _, body in sections)
    new = f"{preamble.rstrip()}\n\n{merged}\n"
    if dry_run:
      print(decision_body)
    else:
      DECISION_CHANGELOG.write_text(new, encoding="utf-8")

  if not dry_run:
    for path in _fragment_files(RELEASE_FRAGMENTS):
      path.unlink()
    for path in _fragment_files(DECISION_FRAGMENTS):
      path.unlink()
    print(f"changelog: assembled {version} and removed its fragments")
  return 0


def main(argv: list[str] | None = None) -> int:
  """Run the command-line interface; return the process exit status."""
  parser = argparse.ArgumentParser(description=__doc__)
  group = parser.add_mutually_exclusive_group(required=True)
  group.add_argument(
    "--check", action="store_true", help="validate fragments and exit"
  )
  group.add_argument(
    "--preview", action="store_true", help="print what assembly would render"
  )
  group.add_argument("--release", metavar="VERSION", help="assemble under VERSION")
  parser.add_argument("--date", help="release date (YYYY-MM-DD)")
  parser.add_argument(
    "--dry-run", action="store_true", help="with --release, print instead of write"
  )
  args = parser.parse_args(argv)

  if args.check:
    problems = check()
    for problem in problems:
      print(f"changelog: {problem}", file=sys.stderr)
    if problems:
      return 1
    pending = len(_fragment_files(RELEASE_FRAGMENTS)) + len(
      _fragment_files(DECISION_FRAGMENTS)
    )
    print(f"changelog: {pending} fragments valid")
    return 0

  if args.preview:
    problems = check()
    for problem in problems:
      print(f"changelog: {problem}", file=sys.stderr)
    if problems:
      return 1
    release_body = render_release_sections()
    decision_body = render_decision_sections()
    if release_body:
      print("=== CHANGELOG.md ===\n")
      print(release_body)
    if decision_body:
      print("\n=== docs/decisions/CHANGELOG.md ===\n")
      print(decision_body)
    return 0

  if not args.date:
    parser.error("--release requires --date")
  return release(args.release, args.date, dry_run=args.dry_run)


if __name__ == "__main__":
  raise SystemExit(main())
