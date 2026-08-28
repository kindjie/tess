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
GENERIC_PREFIX = "Header-only C++20 library for grid pathfinding"
SKIPPED_TOP_LEVEL = {"api", "demo", "dev", "latest", "assets", "search"}
VERSION_NAME = re.compile(r"^[0-9]+\.[0-9]+$")
# The 404 template renders for every unknown URL; a page-specific
# description would claim specificity the response cannot have.
EXEMPT = {"404.html"}


def check_site(site: Path) -> list[str]:
  failures: list[str] = []
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
    if not descriptions[0].strip():
      failures.append(f"{label}: description is empty")
    elif descriptions[0].startswith(GENERIC_PREFIX):
      failures.append(f"{label}: generic site description")
  if seen == 0:
    failures.append("no maintained pages were checked; wrong directory?")
  return failures


def main() -> int:
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("site", nargs="?", type=Path, default=Path("build/site"))
  args = parser.parse_args()
  failures = check_site(args.site.resolve())
  for failure in failures:
    print(f"error: {failure}", file=sys.stderr)
  if failures:
    return 1
  print(f"maintained pages in {args.site} carry specific descriptions")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
