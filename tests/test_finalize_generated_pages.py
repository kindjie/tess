"""Tests for generated-page SEO finalization."""

from __future__ import annotations

import gzip
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

import finalize_generated_pages as fgp  # noqa: E402

SITE = "https://tess.owx.dev/"


def _write(path: Path, text: str) -> None:
  path.parent.mkdir(parents=True, exist_ok=True)
  path.write_text(text, encoding="utf-8")


def _page(title: str, extra_head: str = "") -> str:
  return (
    f"<html><head><title>{title}</title>{extra_head}</head>"
    "<body>content</body></html>"
  )


def _make_site(root: Path) -> None:
  _write(root / "index.html", _page("tess"))
  _write(
    root / "sitemap.xml",
    f'<?xml version="1.0"?><urlset><url><loc>{SITE}</loc></url></urlset>',
  )
  (root / "sitemap.xml.gz").write_bytes(
    gzip.compress((root / "sitemap.xml").read_bytes(), mtime=0)
  )
  _write(root / "api" / "index.html", _page("tess: Main Page"))
  _write(root / "api" / "annotated.html", _page("tess: Class List"))
  _write(root / "api" / "functions_b.html", _page("tess: Data Fields"))
  _write(
    root / "api" / "classtess_1_1Shape-members.html",
    _page("tess: Member List"),
  )
  _write(
    root / "demo" / "index.html",
    _page(
      "tess demos",
      '<meta name="description" content="Header-only C++20 library">'
      '<meta property="og:title" content="tess">',
    ),
  )
  _write(root / "demo" / "tower" / "index.html", _page("tess tower"))


def test_finalize_stamps_documents_and_noindexes_utility_pages(tmp_path):
  _make_site(tmp_path)

  stamped = fgp.finalize(tmp_path, "dev")

  annotated = (tmp_path / "api" / "annotated.html").read_text()
  assert (
    f'<link rel="canonical" href="{SITE}dev/api/annotated.html">' in annotated
  )
  assert (
    'content="tess C++ API reference: Class List."' in annotated
  )
  assert f'<meta property="og:url" content="{SITE}dev/api/annotated.html"' in (
    annotated
  )
  assert f'content="{SITE}assets/tess-social-preview.png"' in annotated

  index = (tmp_path / "api" / "index.html").read_text()
  assert f'<link rel="canonical" href="{SITE}dev/api/">' in index

  for name in ("functions_b.html", "classtess_1_1Shape-members.html"):
    text = (tmp_path / "api" / name).read_text()
    assert fgp.NOINDEX in text
    assert "canonical" not in text

  assert f"{SITE}dev/api/annotated.html" in stamped
  assert all("functions_b" not in url for url in stamped)


def test_finalize_gives_each_demo_its_registered_description(tmp_path):
  _make_site(tmp_path)

  fgp.finalize(tmp_path, "dev")

  tower = (tmp_path / "demo" / "tower" / "index.html").read_text()
  assert "3D tower demo" in tower
  assert f'<link rel="canonical" href="{SITE}dev/demo/tower/">' in tower

  index = (tmp_path / "demo" / "index.html").read_text()
  # The generic description is replaced, not joined, and the existing
  # og:title from the source template is not duplicated.
  assert 'content="Header-only C++20 library"' not in index
  assert index.count('property="og:title"') == 1


def test_finalize_rejects_an_unregistered_demo(tmp_path):
  _make_site(tmp_path)
  _write(tmp_path / "demo" / "novel" / "index.html", _page("tess novel"))

  try:
    fgp.finalize(tmp_path, "dev")
  except fgp.FinalizeError as error:
    assert "novel" in str(error)
  else:
    raise AssertionError("unregistered demo must fail the build")


def test_finalize_extends_both_sitemap_forms_identically(tmp_path):
  _make_site(tmp_path)

  stamped = fgp.finalize(tmp_path, "dev")

  xml = (tmp_path / "sitemap.xml").read_text()
  gz = gzip.decompress((tmp_path / "sitemap.xml.gz").read_bytes()).decode()
  assert xml == gz
  for url in stamped:
    assert f"<loc>{url}</loc>" in xml


def test_finalize_fails_when_a_page_evades_both_categories(tmp_path):
  """The stamped-XOR-listed partition is enforced, not assumed."""
  _make_site(tmp_path)
  # A page with no </head> cannot be stamped; finalize must fail rather
  # than skip it into an unclassified third state.
  _write(tmp_path / "api" / "broken.html", "<html><body>no head</body></html>")

  try:
    fgp.finalize(tmp_path, "dev")
  except fgp.FinalizeError as error:
    assert "broken" in str(error) or "</head>" in str(error)
  else:
    raise AssertionError("an unstampable page must fail the build")


def test_cli_reports_the_stamp_count(tmp_path):
  _make_site(tmp_path)
  tool = Path(__file__).resolve().parents[1] / "tools"
  result = subprocess.run(
    [
      sys.executable,
      str(tool / "finalize_generated_pages.py"),
      str(tmp_path),
      "--version",
      "1.0",
    ],
    check=False,
    capture_output=True,
    text=True,
  )

  assert result.returncode == 0, result.stderr
  assert "finalized" in result.stdout
  assert "/1.0/" in result.stdout


def test_finalize_replaces_multiline_description_and_og_tags(tmp_path):
  """The real demo templates split attributes across lines.

  A single-line pattern matched none of them, so the registered
  description would have been added beside the old one instead of
  replacing it.
  """
  _make_site(tmp_path)
  _write(
    tmp_path / "demo" / "colony" / "index.html",
    "<html><head><title>tess colony demo</title>\n"
    '    <meta name="description"\n'
    '          content="Interactive tess WebAssembly colony simulation\n'
    '                   at scale">\n'
    '    <meta property="og:title"\n'
    '          content="tess colony">\n'
    "</head><body>x</body></html>",
  )

  fgp.finalize(tmp_path, "dev")

  colony = (tmp_path / "demo" / "colony" / "index.html").read_text()
  assert "Interactive tess WebAssembly colony simulation" not in colony
  assert "Colony simulation demo" in colony
  assert colony.count('name="description"') == 1
  assert colony.count('property="og:title"') == 1


def test_finalize_noindexes_namespace_member_indexes(tmp_path):
  _make_site(tmp_path)
  for name in ("namespacemembers.html", "namespacemembers_func.html"):
    _write(tmp_path / "api" / name, _page("tess: Namespace Members"))

  stamped = fgp.finalize(tmp_path, "dev")

  for name in ("namespacemembers.html", "namespacemembers_func.html"):
    text = (tmp_path / "api" / name).read_text()
    assert fgp.NOINDEX in text
    assert "canonical" not in text
  assert all("namespacemembers" not in url for url in stamped)


def test_finalize_rejects_a_titleless_page(tmp_path):
  """No silent 'tess' fallback: a broken page shape fails the build."""
  _make_site(tmp_path)
  _write(
    tmp_path / "api" / "untitled.html",
    "<html><head></head><body>x</body></html>",
  )

  try:
    fgp.finalize(tmp_path, "dev")
  except fgp.FinalizeError as error:
    assert "untitled" in str(error)
  else:
    raise AssertionError("a titleless page must fail the build")
