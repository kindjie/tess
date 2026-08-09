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
OPTLOG_FRAGMENTS = REPO_ROOT / "docs" / "planning" / "optimization-log.d"
OPTIMIZATION_LOG = REPO_ROOT / "docs" / "planning" / "optimization-log.md"

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


def validate_dated_fragment(path: Path) -> str:
  """Return the date for `path`, or raise FragmentError.

  Shared by the decision changelog and the optimization log: both are
  newest-first sequences of `## YYYY-MM-DD - Title` sections, so both want
  one dated section per fragment and nothing else.
  """
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
  # Shape is not enough: 2026-02-30 matches the pattern, passes --check,
  # and release would merge it permanently into a maintained chronology
  # that is sorted by these strings.
  try:
    datetime.date.fromisoformat(date)
  except ValueError:
    raise FragmentError(
      f"{path.name}: {date} is not a calendar date"
    ) from None
  expected = f"## {date} - "
  if not stripped.startswith(expected):
    raise FragmentError(
      f"{path.name}: must open with {expected!r} so the assembled file "
      f"keeps one heading style and the date matches the filename"
    )
  headings = heading_lines(body)
  if len(headings) != 1:
    # A second heading -- including one quoted at column 0 inside a fence
    # that this parser skips, or an unfenced one -- would split the
    # fragment into two sections at release and relocate half of it.
    raise FragmentError(
      f"{path.name}: contains {len(headings)} section headings; a fragment "
      f"must hold exactly one. Indent a quoted heading, or split the file."
    )
  return date


# The optimization log's fragments obey the same rules; kept as a name so
# call sites read for what they validate rather than which file they share.
validate_decision_fragment = validate_dated_fragment
validate_optlog_fragment = validate_dated_fragment


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
  for path in _fragment_files(OPTLOG_FRAGMENTS):
    try:
      validate_optlog_fragment(path)
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

  stray: list[str] = []
  for line in body.splitlines():
    if line.startswith("### "):
      flush()
      buffer = []
      current = line[4:].strip().lower()
      continue
    if current is None:
      if line.strip():
        stray.append(line)
      continue
    buffer.append(line)
  flush()
  if stray:
    # Dropping this silently is the failure this whole mechanism exists to
    # prevent, so refuse rather than lose it.
    raise FragmentError(
      "CHANGELOG.md has content under [Unreleased] that belongs to no "
      f"'### Category' heading and would be lost: {stray[0][:60]!r}"
    )
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


def render_dated_sections(directory: Path) -> str:
  """Render pending dated fragments from `directory`, newest first."""
  paths = _fragment_files(directory)
  for path in paths:
    validate_dated_fragment(path)
  ordered = sorted(paths, key=lambda path: path.name, reverse=True)
  return "\n\n".join(
    path.read_text(encoding="utf-8").strip() for path in ordered
  )


def render_decision_sections() -> str:
  """Render pending decision fragments, newest first."""
  return render_dated_sections(DECISION_FRAGMENTS)


def render_optlog_sections() -> str:
  """Render pending optimization-log fragments, newest first."""
  return render_dated_sections(OPTLOG_FRAGMENTS)


DATED_HEADING_RE = re.compile(r"^## (\d{4}-\d{2}-\d{2}) - ")
FENCE_RE = re.compile(r"^\s*(```|~~~)")


def heading_lines(text: str) -> list[tuple[int, str | None]]:
  """Line indices that open a section, skipping fenced blocks.

  A changelog quotes headings inside fences -- the decisions file ships a
  Template block that does exactly that. Matching them as real headings
  splits a section in half and relocates its body.
  """
  found: list[tuple[int, str | None]] = []
  in_fence = False
  for index, line in enumerate(text.splitlines()):
    if FENCE_RE.match(line):
      in_fence = not in_fence
      continue
    if in_fence:
      continue
    match = DATED_HEADING_RE.match(line)
    if match:
      found.append((index, match.group(1)))
    elif line.startswith("## "):
      found.append((index, None))
  return found


def split_dated_sections(text: str) -> list[tuple[str, str]]:
  """Split a decision changelog body into [(date, section-text), ...]."""
  lines = text.splitlines()
  starts = [(index, date) for index, date in heading_lines(text) if date]
  sections: list[tuple[str, str]] = []
  for position, (start, date) in enumerate(starts):
    end = starts[position + 1][0] if position + 1 < len(starts) else len(lines)
    sections.append((date, "\n".join(lines[start:end]).strip("\n")))
  return sections


def merge_dated_document(document: Path, fragment_body: str) -> str | None:
  """Fold `fragment_body` into a newest-first dated document.

  Returns the new text, or None after reporting why it cannot be built.
  Sorting the combined list rather than prepending keeps the file ordered
  even when a fragment is dated earlier than the newest entry already in
  it -- which happens whenever a branch sits unmerged across a day.
  """
  text = document.read_text(encoding="utf-8")
  existing = split_dated_sections(text)
  if not existing:
    print(
      f"changelog: {document.name} has no dated sections",
      file=sys.stderr,
    )
    return None
  first = text.index(f"## {existing[0][0]} - ")
  sections = existing + split_dated_sections(fragment_body)
  sections.sort(key=lambda item: item[0], reverse=True)
  merged = "\n\n".join(body for _, body in sections)
  return f"{text[:first].rstrip()}\n\n{merged}\n"


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
  optlog_body = render_optlog_sections()
  if not release_body and not decision_body and not optlog_body:
    print("changelog: no fragments to assemble", file=sys.stderr)
    return 1

  # Compute both documents in full before touching either. A failure while
  # rendering the second one must not leave the first already released.
  pending: list[tuple[Path, str]] = []
  heading = f"## [{version}] - {date}"

  text = RELEASE_CHANGELOG.read_text(encoding="utf-8")
  if UNRELEASED_HEADING not in text:
    print(
      f"changelog: {RELEASE_CHANGELOG.name} has no {UNRELEASED_HEADING}",
      file=sys.stderr,
    )
    return 1
  if heading in text:
    print(f"changelog: {heading!r} already exists", file=sys.stderr)
    return 1
  head, _, tail = text.partition(UNRELEASED_HEADING)
  next_release = tail.find("\n## ")
  if next_release == -1:
    unreleased_body, remainder = tail, ""
  else:
    unreleased_body, remainder = tail[:next_release], tail[next_release + 1 :]
  try:
    merged = render_release_sections(parse_sections(unreleased_body))
  except FragmentError as error:
    print(f"changelog: {error}", file=sys.stderr)
    return 1
  # A decisions-only release must still fold the Unreleased body, or those
  # entries silently stay unreleased while the run reports success.
  section = f"{heading}\n\n{merged}\n" if merged else ""
  if section:
    pending.append(
      (RELEASE_CHANGELOG,
       f"{head}{UNRELEASED_HEADING}\n\n{section}\n{remainder}")
    )

  for document, body in (
    (DECISION_CHANGELOG, decision_body),
    (OPTIMIZATION_LOG, optlog_body),
  ):
    if not body:
      continue
    merged_document = merge_dated_document(document, body)
    if merged_document is None:
      return 1
    pending.append((document, merged_document))

  if dry_run:
    for _, body in pending:
      print(body[:2000])
    return 0

  for path, body in pending:
    path.write_text(body, encoding="utf-8")

  if not dry_run:
    for path in _fragment_files(RELEASE_FRAGMENTS):
      path.unlink()
    for path in _fragment_files(DECISION_FRAGMENTS):
      path.unlink()
    for path in _fragment_files(OPTLOG_FRAGMENTS):
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
    pending = sum(
      len(_fragment_files(directory))
      for directory in (RELEASE_FRAGMENTS, DECISION_FRAGMENTS,
                        OPTLOG_FRAGMENTS)
    )
    print(f"changelog: {pending} fragments valid")
    return 0

  if args.preview:
    problems = check()
    for problem in problems:
      print(f"changelog: {problem}", file=sys.stderr)
    if problems:
      return 1
    # Every document release writes, or a preview reports success while
    # showing nothing for the one that is actually pending.
    for heading, body in (
      ("=== CHANGELOG.md ===", render_release_sections()),
      ("=== docs/decisions/CHANGELOG.md ===", render_decision_sections()),
      ("=== docs/planning/optimization-log.md ===",
       render_optlog_sections()),
    ):
      if body:
        print(f"\n{heading}\n")
        print(body)
    return 0

  if not args.date:
    parser.error("--release requires --date")
  return release(args.release, args.date, dry_run=args.dry_run)


if __name__ == "__main__":
  raise SystemExit(main())
