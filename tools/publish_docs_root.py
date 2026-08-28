#!/usr/bin/env python3
"""Assemble and verify Tess's final GitHub Pages publication tree."""

from __future__ import annotations

import argparse
import gzip
import html
import json
from pathlib import Path, PurePosixPath
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
LEGACY_ROOT_ROBOTS = """User-agent: *
Allow: /
# The development tree duplicates released pages; only the release
# trees should be indexed.
Disallow: /dev/
Sitemap: https://tess.owx.dev/latest/sitemap.xml
"""
# The root robots file is written here rather than inherited from the
# version tree the root is derived from. That tree belongs to a released
# tag, so whatever robots directives were correct at that release stay
# pinned at the root until the next one -- which is how the legacy
# `Disallow: /dev/` above outlived the switch to `noindex, follow` on
# the version trees, and kept crawlers from reading the very noindex
# that was meant to retire them.
ROOT_ROBOTS = """User-agent: *
Allow: /
Sitemap: https://tess.owx.dev/sitemap.xml
"""


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
  if not isinstance(data, dict) or data.get("schema") != 1:
    raise PublicationError(f"invalid {MANIFEST} schema")
  paths = data.get("paths")
  if not isinstance(paths, list) or not all(
    isinstance(item, str)
    and item not in {"", ".", ".."}
    and "\0" not in item
    and not PurePosixPath(item).is_absolute()
    and PurePosixPath(item).parts == (item,)
    for item in paths
  ):
    raise PublicationError(f"invalid {MANIFEST} path inventory")
  if len(set(paths)) != len(paths):
    raise PublicationError(f"duplicate path in {MANIFEST} inventory")
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

  with tempfile.TemporaryDirectory(prefix="tess-root-current-") as temp:
    staged = Path(temp)
    for item in items:
      _copy_item(item, staged / item.name)
    _rewrite_root_copy(staged, _latest_version(pages))
    (staged / "robots.txt").write_text(ROOT_ROBOTS)
    if "robots.txt" not in incoming:
      incoming.append("robots.txt")

    previous = set(owned or [])
    for name in incoming:
      destination = pages / name
      destination_exists = destination.exists() or destination.is_symlink()
      if name in previous or not destination_exists:
        continue
      if (
        owned is None
        and name == "index.html"
        and _looks_like_mike_redirect(destination)
      ):
        continue
      # The pre-migration workflow installed one exact legacy robots file at
      # the root. Claim that known file once; any operator variation remains
      # an unowned collision.
      if (
        owned is None
        and name == "robots.txt"
        and destination.is_file()
        and destination.read_text() == LEGACY_ROOT_ROBOTS
      ):
        continue
      raise PublicationError(
        f"unowned root path would be overwritten: {name}"
      )
    if owned is None:
      owned = []

    for name in owned:
      _remove(pages / name)
    for name in incoming:
      destination = pages / name
      if destination.exists() or destination.is_symlink():
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


def _normalize_root_robots(pages: Path, owned: list[str]) -> None:
  """Bring an already-published root robots.txt up to the current policy.

  The ordinary publication path returns early once the root is
  established, so a root file written by an earlier release would
  otherwise never be revisited. That is how `Disallow: /dev/` outlived
  the switch to `noindex, follow` -- and, until this ran here, why
  writing the policy only on the bootstrap path fixed nothing on the
  live tree.
  """
  robots = pages / "robots.txt"
  owned_robots = "robots.txt" in owned
  if robots.is_symlink():
    # A symlink is replaced, never written through: following it would
    # write outside the publication tree the ownership model bounds.
    if not owned_robots:
      raise PublicationError(
        "root robots.txt is an unowned symlink; resolve it manually "
        "before publishing"
      )
    robots.unlink()
  elif robots.is_file():
    # Byte-identical content is accepted regardless of ownership: there
    # is no operator data an overwrite could destroy, and the moment the
    # policy and an unowned file diverge, the branch below fails closed.
    if robots.read_text() == ROOT_ROBOTS:
      return
    if not owned_robots:
      raise PublicationError(
        "root robots.txt is not manifest-owned and does not match the "
        "current policy; resolve it manually before publishing"
      )
  elif robots.exists():
    raise PublicationError(
      "root robots.txt is not a regular file; resolve it manually "
      "before publishing"
    )
  # A missing file is written whether or not the manifest lists it: an
  # absent name is claimable, exactly as the bootstrap collision rule
  # treats absent destinations, and the final artifact check requires
  # the file to exist.
  robots.write_text(ROOT_ROBOTS)


def sync(pages: Path, root_source: Path | None) -> None:
  """Publish a stable root tree and exact latest-HTML redirects."""
  pages = pages.resolve()
  owned = _read_manifest(pages)
  if root_source is None and owned is not None:
    _normalize_root_robots(pages, owned)
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


# Per-tree copies of these stay in STORAGE (pages.yml commits the branch
# before this runs) but must not be served: a sitemap or llms.txt inside
# a noindexed version tree is an independently indexable resource that
# advertises URLs the HTML policy retires, and a nested robots.txt is a
# directive file nothing should be reading at a subpath.
TREE_UNSERVED = ("sitemap.xml", "sitemap.xml.gz", "llms.txt", "robots.txt")
ANCHOR_RE = re.compile(
  r'(<a\b[^>]*?\bhref=")' + re.escape(SITE_URL) + r'([^"#]+)(#[^"]*)?(")'
)


def _resolves_in_tree(tree: Path, path: str) -> bool:
  """Whether site-relative `path` names a page or file in `tree`."""
  if not path or path.startswith(".."):
    return False
  candidate = tree / path
  if path.endswith("/"):
    return (candidate / "index.html").is_file()
  return candidate.is_file() or (candidate / "index.html").is_file()


def _localize_tree_anchors(text: str, tree: Path) -> str:
  """Point a tree's same-origin anchors back into the tree itself.

  Anchor hrefs only -- `link` and `meta` are deliberately out of scope,
  and the bare origin never matches (`[^"#]+` requires a nonempty
  path): an absolute link to the site root is the reader's escape hatch
  to the stable release and must survive. A numeric tree's anchors were
  written when that tree WAS `latest`, so a leading `latest/` is
  stripped before resolution. An href whose target does not exist in
  the tree is preserved: it is either a deliberate cross-version link
  or points at a page this release never had, and the stable site is
  the right destination for both.
  """
  def replace(match: re.Match[str]) -> str:
    path = match.group(2)
    candidate = path
    if VERSION_NAME.fullmatch(tree.name) and candidate.startswith("latest/"):
      candidate = candidate[len("latest/") :]
    if not _resolves_in_tree(tree, candidate):
      return match.group(0)
    fragment = match.group(3) or ""
    return f"{match.group(1)}/{tree.name}/{candidate}{fragment}{match.group(4)}"

  return ANCHOR_RE.sub(replace, text)


def _repair_stale_head(text: str, tree: Path) -> str:
  """Repoint a frozen tree's stale `latest` head metadata at itself.

  Numeric trees were published while aliased as `latest`, so their
  canonical, `og:url`, structured-data URL, and social images still
  claim `/latest/` (the image malformed as `latestassets`). Those
  claims were correct at publish and are stale now; repairing a stale
  claim to the tree's own URL is not the same act as altering
  current-correct metadata, which this function never touches -- every
  pattern below requires the stale `latest` prefix to match at all.
  """
  own = f"{SITE_URL}{tree.name}/"
  replacements = [
    # The malformed social image: {SITE}latestassets/... lost its slash.
    (
      re.compile(r'(content=")' + re.escape(f"{SITE_URL}latestassets/")),
      rf"\g<1>{own}assets/",
    ),
    (
      re.compile(
        r'(<link rel="canonical" href=")'
        + re.escape(f"{SITE_URL}latest/")
      ),
      rf"\g<1>{own}",
    ),
    (
      re.compile(
        r'(<meta property="og:url" content=")'
        + re.escape(f"{SITE_URL}latest/")
      ),
      rf"\g<1>{own}",
    ),
    # The homepage WebSite structured-data block.
    (
      re.compile(r'("url": ")' + re.escape(f"{SITE_URL}latest/")),
      rf"\g<1>{own}",
    ),
  ]
  for pattern, replacement in replacements:
    text = pattern.sub(replacement, text)
  return text


def prepare_artifact(pages: Path) -> None:
  """Rewrite version trees in the served artifact for non-indexing.

  Storage is already committed when this runs (pages.yml orders the
  branch commit before artifact preparation), so deletions and
  rewrites here shape only what is served.
  """
  for child in pages.iterdir():
    if child.name == "dev" or VERSION_NAME.fullmatch(child.name):
      if not child.is_dir():
        raise PublicationError(f"version path is not a directory: {child}")
      for name in TREE_UNSERVED:
        _remove(child / name)
      numeric = VERSION_NAME.fullmatch(child.name) is not None
      for page in child.rglob("*.html"):
        _insert_noindex(page)
        text = page.read_text()
        if numeric:
          text = _repair_stale_head(text, child)
        page.write_text(_localize_tree_anchors(text, child))
  # `latest/` is redirects: its HTML (and root-absolute fallback
  # anchors) stays untouched, but the unserved resources go here too.
  latest = pages / "latest"
  if latest.is_dir():
    for name in TREE_UNSERVED:
      _remove(latest / name)


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
  # A crawler that is forbidden to fetch a page never reads its
  # `noindex`, so disallowing a version tree pins whatever is already
  # indexed instead of retiring it. The version trees carry `noindex,
  # follow` and must stay crawlable for that to take effect.
  robots = (pages / "robots.txt").read_text()
  for line in robots.splitlines():
    stripped = line.strip()
    if stripped.startswith("Disallow:") and stripped != "Disallow:":
      raise PublicationError(
        f"root robots.txt disallows a path, blocking noindex: {stripped}"
      )
  _assert_contains(pages / "latest" / "index.html", NOINDEX)
  _assert_contains(
    pages / "latest" / "index.html",
    f'content="0; url={SITE_URL}"',
  )
  # Root copies of the indexable resources must exist -- the version
  # trees defer to them.
  for name in ("llms.txt", "sitemap.xml", "sitemap.xml.gz"):
    if not (pages / name).is_file():
      raise PublicationError(f"root {name} is missing")
  gz_urls = set(
    re.findall(
      r"<loc>([^<]+)</loc>",
      gzip.decompress((pages / "sitemap.xml.gz").read_bytes()).decode(),
    )
  )
  xml_urls = set(re.findall(r"<loc>([^<]+)</loc>", sitemap))
  if gz_urls != xml_urls:
    raise PublicationError(
      "root sitemap.xml.gz does not list the same URLs as sitemap.xml"
    )
  # Every page in every version tree, not just each tree's index: a
  # faulty traversal that noindexed only index.html would leave hundreds
  # of nested pages indexable while an index-only check passed. Each
  # walk also proves it saw at least one page, so an empty or mislaid
  # tree cannot pass vacuously.
  for child in sorted(pages.iterdir()):
    if child.name == "dev" or VERSION_NAME.fullmatch(child.name):
      pages_seen = 0
      for page in child.rglob("*.html"):
        _assert_contains(page, NOINDEX)
        pages_seen += 1
      if pages_seen == 0:
        raise PublicationError(f"version tree {child.name} has no HTML")
      for name in TREE_UNSERVED:
        if (child / name).exists():
          raise PublicationError(
            f"served version tree {child.name} still contains {name}"
          )
  latest_seen = 0
  for page in (pages / "latest").rglob("*.html"):
    _assert_contains(page, NOINDEX)
    _assert_contains(page, 'content="0; url=')
    latest_seen += 1
  if latest_seen == 0:
    raise PublicationError("latest tree has no redirect HTML")
  for name in TREE_UNSERVED:
    if (pages / "latest" / name).exists():
      raise PublicationError(f"served latest tree still contains {name}")


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
