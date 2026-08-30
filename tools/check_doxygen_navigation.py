#!/usr/bin/env python3
"""Validate version-relative return links in generated Doxygen menus."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from urllib.parse import urljoin


SITE = "https://tess.owx.dev/"
PREFIXES = ("", "main/", "0.13/", "1.0.0-rc.1/")
EXPECTED = {
  "Docs": "../",
  "Learn": "../getting-started/",
  "Reference": "../reference/",
}
DESTINATIONS = {
  "Docs": "",
  "Learn": "getting-started/",
  "Reference": "reference/",
}
REPRESENTATIVE_PAGES = (
  "index.html",
  "annotated.html",
)
MENU_ENTRY = re.compile(r'\{text:"([^"]+)",url:"([^"]+)"')


def check_navigation(api: Path) -> list[str]:
  """Return failures in one generated Doxygen API directory."""
  failures: list[str] = []
  for relative in REPRESENTATIVE_PAGES:
    if not (api / relative).is_file():
      failures.append(f"generated API page is missing: {relative}")

  menu = api / "menudata.js"
  if not menu.is_file():
    failures.append("generated Doxygen menu is missing: menudata.js")
    return failures

  entries: dict[str, list[str]] = {}
  for title, url in MENU_ENTRY.findall(menu.read_text(encoding="utf-8")):
    entries.setdefault(title, []).append(url)

  for title, expected in EXPECTED.items():
    urls = entries.get(title, [])
    if not urls:
      failures.append(f"menudata.js: missing {title} return tab")
      continue
    if len(urls) != 1:
      failures.append(
        f"menudata.js: {title} return tab appears {len(urls)} times"
      )
      continue
    if urls[0] != expected:
      failures.append(
        f"menudata.js: {title} points to {urls[0]!r}, expected "
        f"{expected!r}"
      )
      continue

    for prefix in PREFIXES:
      source = f"{SITE}{prefix}api/{REPRESENTATIVE_PAGES[1]}"
      resolved = urljoin(source, urls[0])
      wanted = f"{SITE}{prefix}{DESTINATIONS[title]}"
      if resolved != wanted:
        failures.append(
          f"menudata.js: {title} resolves to {resolved!r} under "
          f"/{prefix}, expected {wanted!r}"
        )

  return failures


def main(argv: list[str] | None = None) -> int:
  parser = argparse.ArgumentParser()
  parser.add_argument("api", type=Path)
  args = parser.parse_args(argv)

  failures = check_navigation(args.api)
  for failure in failures:
    print(f"error: {failure}", file=sys.stderr)
  if failures:
    return 1
  print(f"validated generated Doxygen navigation in {args.api}")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
