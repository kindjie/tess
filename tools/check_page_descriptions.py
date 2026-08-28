#!/usr/bin/env python3
"""Require a specific description on every maintained built page.

Issue #287: thirty-one indexable pages shared the generic site
description, so search results and social cards were interchangeable.
A source front-matter test alone cannot prove what ships -- mkdocs
falls back silently -- so this reads the BUILT site and requires every
maintained page to carry exactly one nonempty description that is not
the generic fallback. The generated `api/` and `demo/` trees have their
own stamped contract (`finalize_generated_pages.py`) and version trees
their own retirement contract, so both are out of scope here.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

DESCRIPTION_RE = re.compile(
  r'<meta name="description" content="([^"]*)"'
)
SITE_DESCRIPTION_RE = re.compile(
  r"^site_description:\s*>-?\n((?:[ \t]+\S[^\n]*\n?)+)", re.M
)


def generic_description(repo_root: Path) -> str:
  """The mkdocs fallback, read from configuration rather than pinned.

  A hard-coded copy would go stale the day `site_description` is
  reworded, and the reworded fallback would then pass this check.
  """
  config = (repo_root / "mkdocs.yml").read_text(encoding="utf-8")
  match = SITE_DESCRIPTION_RE.search(config)
  if match is None:
    raise SystemExit("mkdocs.yml has no site_description block")
  return " ".join(match.group(1).split())
SKIPPED_TOP_LEVEL = {"api", "demo", "dev", "latest", "assets", "search"}
VERSION_NAME = re.compile(r"^[0-9]+\.[0-9]+$")
# The 404 template renders for every unknown URL; a page-specific
# description would claim specificity the response cannot have.
EXEMPT = {"404.html"}


def check_site(site: Path, *, generic: str) -> list[str]:
  failures: list[str] = []
  first_page_for: dict[str, str] = {}
  seen = 0
  for page in sorted(site.rglob("*.html")):
    relative = page.relative_to(site)
    top = relative.parts[0]
    if top in SKIPPED_TOP_LEVEL or VERSION_NAME.fullmatch(top):
      continue
    label = relative.as_posix()
    if label in EXEMPT:
      continue
    seen += 1
    descriptions = DESCRIPTION_RE.findall(page.read_text(encoding="utf-8"))
    if len(descriptions) != 1:
      failures.append(
        f"{label}: expected exactly one description, found "
        f"{len(descriptions)}"
      )
      continue
    normalized = " ".join(descriptions[0].split())
    if not normalized:
      failures.append(f"{label}: description is empty")
      continue
    if normalized.startswith(generic):
      failures.append(f"{label}: generic site description")
      continue
    if normalized in first_page_for:
      # Two pages sharing one description recreates the interchangeable
      # snippets this check exists to prevent, one rung up.
      failures.append(
        f"{label}: description duplicates {first_page_for[normalized]}"
      )
    else:
      first_page_for[normalized] = label
  if seen == 0:
    failures.append("no maintained pages were checked; wrong directory?")
  return failures


def main() -> int:
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("site", nargs="?", type=Path, default=Path("build/site"))
  args = parser.parse_args()
  repo_root = Path(__file__).resolve().parents[1]
  failures = check_site(
    args.site.resolve(), generic=generic_description(repo_root)
  )
  for failure in failures:
    print(f"error: {failure}", file=sys.stderr)
  if failures:
    return 1
  print(f"maintained pages in {args.site} carry specific descriptions")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
