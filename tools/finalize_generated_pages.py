#!/usr/bin/env python3
"""Stamp SEO metadata onto the generated API and demo pages.

mkdocs writes head metadata and the sitemap before the Doxygen API
reference and the WebAssembly demos are copied into the site, so those
pages shipped with no canonical identity, no description, no social
metadata, and no sitemap presence -- 776 API pages and every demo page,
invisible to structured discovery.

This runs on `build/site` immediately after the generated trees are
copied in and before anything consumes them. Every downstream copy then
inherits the stamps: the link-checked verification site, the uploaded
artifact, the `docs/api` + `docs/demo` trees staged for mike's version
build, and -- on an alias-moving release -- the stable root assembled
from the same artifact via `--root-source` (whose existing rewrite
turns the version-prefixed canonical into the root form).

The same invocation verifies its own postconditions and fails the build
otherwise: every generated HTML page is either stamped or explicitly
noindex-listed (never neither, never both), the sitemap gains exactly
the stamped set, and every enumeration saw at least one page.
"""

from __future__ import annotations

import argparse
import fnmatch
import gzip
import re
import sys
from pathlib import Path

SITE_URL = "https://tess.owx.dev/"
NOINDEX = '<meta name="robots" content="noindex, follow">'
SOCIAL_IMAGE = f"{SITE_URL}assets/tess-social-preview.png"
TITLE_RE = re.compile(r"<title>([^<]*)</title>")
# \s matches newlines: the real demo templates split the attributes
# across lines, and one content value wraps -- a single-line pattern
# would silently keep the old description beside the new one.
DESCRIPTION_RE = re.compile(
  r'<meta\s+name="description"\s+content="[^"]*"\s*/?>'
)
OG_REPLACED_RE = re.compile(
  r'<meta\s+property="og:(?:title|url)"\s+content="[^"]*"\s*/?>'
)
HEAD_END = "</head>"

# Doxygen's utility and index surfaces: high-volume, near-duplicate
# navigation pages a search result should never land on. Everything not
# matched here is a document -- class, file, namespace, page -- and is
# stamped for indexing. The verify pass enforces that the two sets
# partition the tree, so an unclassified new page shape fails the build
# instead of shipping unstamped.
API_NOINDEX_PATTERNS = (
  "functions*.html",
  "globals*.html",
  "namespacemembers*.html",
  "*-members.html",
  "navtree*.html",
  "search/*.html",
)

# One description per demo, keyed by the directory under demo/. A demo
# added without an entry fails the build here rather than shipping with
# the generic site description.
DEMO_DESCRIPTIONS = {
  "flow-steering": (
    "Interactive tess flow field steering tutorial using public distance "
    "labels without retaining per-tile directions."
  ),
  "sparse-stream": (
    "Procedural sparse-world demo: tess streams deterministic terrain "
    "through a bounded 32-page resident set in WebAssembly."
  ),
  "": (
    "Interactive tess demos: pathfinding, colony simulation, congestion "
    "pricing, and a 3D tower, all running the C++ library in WebAssembly."
  ),
  "colony": (
    "Colony simulation demo: hundreds of tess path agents with retained "
    "routes, joint movement, and live replanning in WebAssembly."
  ),
  "congestion": (
    "Congestion pricing demo: demand-driven cost fields reroute tess "
    "path agents around crowded tiles, live in WebAssembly."
  ),
  "tower": (
    "3D tower demo: tess path agents route across six floors with "
    "stairwell congestion pricing, live in WebAssembly."
  ),
  "strategies": (
    "Strategy comparison demo: A*, route caches, weighted batches, and "
    "distance fields over one obstacle map in WebAssembly."
  ),
  "traffic": (
    "Traffic demo: tess weighted routing under live cost-field edits, "
    "running in WebAssembly."
  ),
  "diagnostics": (
    "Diagnostics demo: tess instrumentation counters and trace overlays "
    "on a live simulation in WebAssembly."
  ),
  "webgpu": (
    "WebGPU demo: the tess GPU descriptor interface driving a browser "
    "compute pipeline from WebAssembly."
  ),
}


class FinalizeError(Exception):
  """The generated trees do not satisfy the stamping contract."""


def _canonical_url(version: str, relative: Path) -> str:
  posix = relative.as_posix()
  if relative.name == "index.html":
    parent = posix[: -len("index.html")]
    return f"{SITE_URL}{version}/{parent}"
  return f"{SITE_URL}{version}/{posix}"


def _api_description(title: str) -> str:
  suffix = title.removeprefix("tess:").strip()
  if suffix:
    return f"tess C++ API reference: {suffix}."
  return "tess C++ API reference."


def _stamp(page: Path, url: str, title: str, description: str) -> None:
  text = page.read_text(encoding="utf-8")
  if HEAD_END not in text:
    raise FinalizeError(f"{page}: no </head> to stamp")
  if 'rel="canonical"' in text:
    raise FinalizeError(f"{page}: already carries a canonical")
  description = description.replace('"', "&quot;")
  head = (
    f'<link rel="canonical" href="{url}">\n'
    f'<meta name="description" content="{description}">\n'
    f'<meta property="og:title" content="{title}">\n'
    f'<meta property="og:url" content="{url}">\n'
    f'<meta property="og:image" content="{SOCIAL_IMAGE}">\n'
  )
  # A generated page may already carry the generic site description or
  # its template's own og:title; the specific stamps replace them
  # rather than standing beside them as duplicates.
  text = DESCRIPTION_RE.sub("", text, count=1)
  text = OG_REPLACED_RE.sub("", text)
  page.write_text(text.replace(HEAD_END, head + HEAD_END, 1), encoding="utf-8")


def _noindex(page: Path) -> None:
  text = page.read_text(encoding="utf-8")
  if NOINDEX in text:
    return
  if HEAD_END not in text:
    raise FinalizeError(f"{page}: no </head> to stamp")
  page.write_text(
    text.replace(HEAD_END, f"{NOINDEX}\n{HEAD_END}", 1), encoding="utf-8"
  )


def _title_of(page: Path) -> str:
  match = TITLE_RE.search(page.read_text(encoding="utf-8"))
  title = match.group(1).strip() if match else ""
  if not title:
    # A silent "tess" fallback would stamp a broken page shape with
    # generic metadata and sail through _verify -- the exact class of
    # quiet degradation this pass exists to refuse.
    raise FinalizeError(f"{page}: no usable <title> to derive metadata from")
  return title


def finalize(site: Path, version: str) -> list[str]:
  """Stamp both generated trees; return the canonical URLs added."""
  stamped: list[str] = []
  api = site / "api"
  demo = site / "demo"
  if not api.is_dir() or not demo.is_dir():
    raise FinalizeError(f"{site}: generated api/ and demo/ trees missing")

  api_pages = sorted(api.rglob("*.html"))
  if not api_pages:
    raise FinalizeError("api tree has no HTML")
  for page in api_pages:
    inside = page.relative_to(api).as_posix()
    if any(fnmatch.fnmatch(inside, p) for p in API_NOINDEX_PATTERNS):
      _noindex(page)
      continue
    url = _canonical_url(version, page.relative_to(site))
    _stamp(page, url, _title_of(page), _api_description(_title_of(page)))
    stamped.append(url)

  demo_pages = sorted(demo.rglob("*.html"))
  if not demo_pages:
    raise FinalizeError("demo tree has no HTML")
  for page in demo_pages:
    relative = page.relative_to(demo)
    key = relative.parts[0] if len(relative.parts) > 1 else ""
    if key not in DEMO_DESCRIPTIONS:
      raise FinalizeError(
        f"demo/{relative.as_posix()}: no description registered for "
        f"'{key}' -- add one to DEMO_DESCRIPTIONS"
      )
    url = _canonical_url(version, page.relative_to(site))
    _stamp(page, url, _title_of(page), DEMO_DESCRIPTIONS[key])
    stamped.append(url)

  _extend_sitemap(site, stamped)
  _verify(site, version, stamped)
  return stamped


def _extend_sitemap(site: Path, urls: list[str]) -> None:
  sitemap = site / "sitemap.xml"
  if not sitemap.is_file():
    raise FinalizeError("sitemap.xml is missing")
  text = sitemap.read_text(encoding="utf-8")
  if "</urlset>" not in text:
    raise FinalizeError("sitemap.xml has no </urlset>")
  entries = "".join(f"<url><loc>{url}</loc></url>" for url in urls)
  text = text.replace("</urlset>", f"{entries}</urlset>", 1)
  sitemap.write_text(text, encoding="utf-8")
  (site / "sitemap.xml.gz").write_bytes(
    gzip.compress(text.encode("utf-8"), mtime=0)
  )


def _verify(site: Path, version: str, stamped: list[str]) -> None:
  """Fail unless stamping partitioned the trees and reached the sitemap."""
  problems: list[str] = []
  if not stamped:
    problems.append("no page was stamped; the enumeration is broken")
  sitemap = (site / "sitemap.xml").read_text(encoding="utf-8")
  for url in stamped:
    if f"<loc>{url}</loc>" not in sitemap:
      problems.append(f"stamped page missing from sitemap: {url}")
  gz = gzip.decompress((site / "sitemap.xml.gz").read_bytes()).decode()
  if set(re.findall(r"<loc>([^<]+)</loc>", gz)) != set(
    re.findall(r"<loc>([^<]+)</loc>", sitemap)
  ):
    problems.append("sitemap.xml.gz diverges from sitemap.xml")
  for tree in ("api", "demo"):
    for page in (site / tree).rglob("*.html"):
      text = page.read_text(encoding="utf-8")
      has_canonical = 'rel="canonical"' in text
      has_noindex = NOINDEX in text
      if has_canonical == has_noindex:
        problems.append(
          f"{page.relative_to(site).as_posix()}: must be stamped XOR "
          f"noindex-listed (canonical={has_canonical}, "
          f"noindex={has_noindex})"
        )
  if problems:
    raise FinalizeError("; ".join(problems[:20]))


def main() -> int:
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("site", type=Path)
  parser.add_argument("--version", required=True)
  args = parser.parse_args()
  try:
    stamped = finalize(args.site.resolve(), args.version)
  except (FinalizeError, OSError) as error:
    print(f"finalize error: {error}", file=sys.stderr)
    return 1
  print(
    f"finalized {len(stamped)} generated pages for /{args.version}/ "
    "and extended the sitemap"
  )
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
