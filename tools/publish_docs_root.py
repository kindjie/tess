#!/usr/bin/env python3
"""Assemble and verify Tess's final GitHub Pages publication tree."""

from __future__ import annotations

import argparse
import gzip
import html
import json
from pathlib import Path
import re
import shutil
import sys
import tempfile


SITE_URL = "https://tess.owx.dev/"
MANIFEST = ".tess-root-current.json"
RESERVED_NAMES = {
  ".git",
  ".nojekyll",
  MANIFEST,
  "dev",
  "latest",
  "versions.json",
}
VERSION_NAME = re.compile(r"^[0-9]+\.[0-9]+$")
TEXT_SUFFIXES = {
  ".css",
  ".html",
  ".js",
  ".json",
  ".txt",
  ".xml",
}
NOINDEX = '<meta name="robots" content="noindex, follow">'


class PublicationError(RuntimeError):
  """A publication tree violated the fail-closed contract."""


def _reserved(name: str) -> bool:
  return name in RESERVED_NAMES or VERSION_NAME.fullmatch(name) is not None


def _read_manifest(pages: Path) -> list[str] | None:
  path = pages / MANIFEST
  if not path.exists():
    return None
  try:
    data = json.loads(path.read_text())
  except (json.JSONDecodeError, OSError) as error:
    raise PublicationError(f"invalid {MANIFEST}: {error}") from error
  paths = data.get("paths")
  if not isinstance(paths, list) or not all(
    isinstance(item, str) and item and "/" not in item for item in paths
  ):
    raise PublicationError(f"invalid {MANIFEST} path inventory")
  if any(_reserved(item) for item in paths):
    raise PublicationError(f"{MANIFEST} claims a reserved path")
  return paths


def _write_manifest(pages: Path, names: list[str]) -> None:
  payload = {"schema": 1, "paths": sorted(names)}
  (pages / MANIFEST).write_text(json.dumps(payload, indent=2) + "\n")


def _remove(path: Path) -> None:
  if path.is_dir() and not path.is_symlink():
    shutil.rmtree(path)
  elif path.exists() or path.is_symlink():
    path.unlink()


def _copy_item(source: Path, destination: Path) -> None:
  if source.is_symlink():
    raise PublicationError(f"root source contains a symlink: {source.name}")
  if source.is_dir():
    shutil.copytree(source, destination)
  else:
    shutil.copy2(source, destination)


def _source_items(source: Path) -> list[Path]:
  if not source.is_dir():
    raise PublicationError(f"root source is not a directory: {source}")
  items = sorted(source.iterdir(), key=lambda item: item.name)
  if not items or not (source / "index.html").is_file():
    raise PublicationError("root source is empty or lacks index.html")
  collisions = [item.name for item in items if _reserved(item.name)]
  if collisions:
    raise PublicationError(
      "root source contains reserved paths: " + ", ".join(collisions)
    )
  return items


def _latest_version(pages: Path) -> str | None:
  try:
    versions = json.loads((pages / "versions.json").read_text())
  except (FileNotFoundError, json.JSONDecodeError, OSError):
    return None
  for entry in versions:
    if "latest" in entry.get("aliases", []):
      version = entry.get("version")
      return version if isinstance(version, str) else None
  return None


def _rewrite_text(
  text: str, version: str | None, *, strip_noindex: bool = True
) -> str:
  replacements = [
    (f"{SITE_URL}latestassets/", f"{SITE_URL}assets/"),
    (f"{SITE_URL}latest/", SITE_URL),
  ]
  if version:
    replacements.append((f"{SITE_URL}{version}/", SITE_URL))
  for old, new in replacements:
    text = text.replace(old, new)
  if strip_noindex:
    text = text.replace(NOINDEX, "")
  return text


def _rewrite_root_copy(
  root: Path, version: str | None, *, strip_noindex: bool = True
) -> None:
  for path in root.rglob("*"):
    if not path.is_file():
      continue
    if path.name == "sitemap.xml.gz":
      content = gzip.decompress(path.read_bytes()).decode()
      rewritten = _rewrite_text(
        content, version, strip_noindex=strip_noindex
      ).encode()
      path.write_bytes(gzip.compress(rewritten, mtime=0))
    elif path.suffix in TEXT_SUFFIXES:
      try:
        content = path.read_text()
      except UnicodeDecodeError:
        continue
      path.write_text(
        _rewrite_text(content, version, strip_noindex=strip_noindex)
      )


def _looks_like_mike_redirect(path: Path) -> bool:
  if not path.is_file():
    return False
  text = path.read_text(errors="replace")
  return "window.location.replace" in text and "latest/" in text


def _sync_root(pages: Path, source: Path) -> None:
  owned = _read_manifest(pages)
  items = _source_items(source)
  incoming = [item.name for item in items]

  if owned is None:
    for name in incoming:
      destination = pages / name
      if not destination.exists():
        continue
      if name == "index.html" and _looks_like_mike_redirect(destination):
        continue
      raise PublicationError(f"unowned root path would be overwritten: {name}")
    owned = []

  with tempfile.TemporaryDirectory(prefix="tess-root-current-") as temp:
    staged = Path(temp)
    for item in items:
      _copy_item(item, staged / item.name)
    _rewrite_root_copy(staged, _latest_version(pages))

    for name in owned:
      _remove(pages / name)
    for name in incoming:
      destination = pages / name
      if destination.exists():
        _remove(destination)
      _copy_item(staged / name, destination)
  _write_manifest(pages, incoming)


def _redirect_html(target: str) -> str:
  escaped = html.escape(target, quote=True)
  encoded = json.dumps(target)
  return f"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <title>Redirecting to tess documentation</title>
  {NOINDEX}
  <link rel="canonical" href="{escaped}">
  <meta http-equiv="refresh" content="0; url={escaped}">
  <script>
    const target = new URL({encoded});
    target.search = window.location.search;
    target.hash = window.location.hash;
    window.location.replace(target.href);
  </script>
</head>
<body>
  <p>This page moved to <a href="{escaped}">{escaped}</a>.</p>
</body>
</html>
"""


def _redirect_latest(pages: Path) -> None:
  latest = pages / "latest"
  if not latest.is_dir():
    raise PublicationError("latest version tree is missing")
  for page in latest.rglob("*.html"):
    relative = page.relative_to(latest)
    if relative.name == "index.html":
      parent = relative.parent.as_posix().rstrip(".")
      target_path = f"{parent}/" if parent else ""
    else:
      target_path = relative.as_posix()
    page.write_text(_redirect_html(SITE_URL + target_path))
  _rewrite_root_copy(latest, _latest_version(pages), strip_noindex=False)


def sync(pages: Path, root_source: Path | None) -> None:
  """Publish a stable root tree and exact latest-HTML redirects."""
  pages = pages.resolve()
  if root_source is None and _read_manifest(pages) is not None:
    return
  source = root_source.resolve() if root_source else pages / "latest"
  _sync_root(pages, source)
  _redirect_latest(pages)


def _insert_noindex(path: Path) -> None:
  text = path.read_text()
  if NOINDEX in text:
    return
  marker = "</head>"
  if marker not in text:
    raise PublicationError(f"HTML lacks </head>: {path}")
  path.write_text(text.replace(marker, f"  {NOINDEX}\n{marker}", 1))


def prepare_artifact(pages: Path) -> None:
  """Add non-indexing metadata to version trees in the served artifact."""
  for child in pages.iterdir():
    if child.name == "dev" or VERSION_NAME.fullmatch(child.name):
      if not child.is_dir():
        raise PublicationError(f"version path is not a directory: {child}")
      for page in child.rglob("*.html"):
        _insert_noindex(page)


def _assert_contains(path: Path, needle: str) -> None:
  if not path.is_file() or needle not in path.read_text():
    raise PublicationError(f"{path} does not contain {needle!r}")


def check(pages: Path) -> None:
  """Fail unless the final Pages artifact has the intended URL contract."""
  root_html = (pages / "index.html").read_text()
  if f'href="{SITE_URL}"' not in root_html:
    raise PublicationError("root index has no stable canonical URL")
  if "noindex" in root_html:
    raise PublicationError("root index must be indexable")
  if f'content="{SITE_URL}assets/' not in root_html:
    raise PublicationError("root index has no root-relative social image")
  if not (pages / "versions.json").is_file():
    raise PublicationError("version-selector inventory is missing")
  _assert_contains(pages / "sitemap.xml", f"<loc>{SITE_URL}</loc>")
  sitemap = (pages / "sitemap.xml").read_text()
  if f"{SITE_URL}latest/" in sitemap or f"{SITE_URL}dev/" in sitemap:
    raise PublicationError("root sitemap contains a noncanonical version URL")
  _assert_contains(pages / "robots.txt", f"Sitemap: {SITE_URL}sitemap.xml")
  _assert_contains(pages / "latest" / "index.html", NOINDEX)
  _assert_contains(
    pages / "latest" / "index.html",
    f'content="0; url={SITE_URL}"',
  )
  for child in pages.iterdir():
    if child.name == "dev" or VERSION_NAME.fullmatch(child.name):
      _assert_contains(child / "index.html", NOINDEX)


def parse_args() -> argparse.Namespace:
  """Parse the publication subcommand and tree paths."""
  parser = argparse.ArgumentParser(description=__doc__)
  subparsers = parser.add_subparsers(dest="command", required=True)
  sync_parser = subparsers.add_parser("sync")
  sync_parser.add_argument("pages", type=Path)
  sync_parser.add_argument("--root-source", type=Path)
  for command in ("prepare-artifact", "check"):
    child = subparsers.add_parser(command)
    child.add_argument("pages", type=Path)
  return parser.parse_args()


def main() -> int:
  """Run the requested publication operation."""
  args = parse_args()
  try:
    if args.command == "sync":
      sync(args.pages, args.root_source)
    elif args.command == "prepare-artifact":
      prepare_artifact(args.pages)
    else:
      check(args.pages)
  except (PublicationError, OSError) as error:
    print(f"publication error: {error}", file=sys.stderr)
    return 1
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
