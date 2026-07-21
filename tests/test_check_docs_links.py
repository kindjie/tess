"""Tests for generated documentation link validation."""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

import check_docs_links as cdl  # noqa: E402


def write_site(root: Path) -> None:
  (root / "guide").mkdir(parents=True)
  (root / "assets").mkdir()
  (root / "index.html").write_text(
    '<a href="guide/#example">Guide</a>'
    '<img src="/assets/logo.svg">'
    '<a href="https://example.com/">External</a>',
    encoding="utf-8",
  )
  (root / "guide" / "index.html").write_text(
    '<h2 id="example">Example</h2><a href="../">Home</a>',
    encoding="utf-8",
  )
  (root / "assets" / "logo.svg").write_text(
    "<svg xmlns=\"http://www.w3.org/2000/svg\"/>",
    encoding="utf-8",
  )


def test_check_site_accepts_local_files_anchors_and_external_links(tmp_path):
  write_site(tmp_path)

  assert cdl.check_site(tmp_path) == []


def test_check_site_reports_missing_local_target(tmp_path):
  write_site(tmp_path)
  (tmp_path / "index.html").write_text(
    '<a href="missing/">Missing</a>', encoding="utf-8"
  )

  assert cdl.check_site(tmp_path) == [
    "index.html: missing local target: missing/"
  ]


def test_check_site_reports_missing_fragment(tmp_path):
  write_site(tmp_path)
  (tmp_path / "index.html").write_text(
    '<a href="guide/#absent">Missing anchor</a>', encoding="utf-8"
  )

  assert cdl.check_site(tmp_path) == [
    "index.html: missing anchor 'absent' in guide/index.html"
  ]


def test_check_site_ignores_only_selected_missing_fragment(tmp_path):
  write_site(tmp_path)
  (tmp_path / "index.html").write_text(
    '<a href="guide/#known">Known issue</a>'
    '<a href="guide/#unexpected">Broken</a>',
    encoding="utf-8",
  )

  assert cdl.check_site(
    tmp_path,
    ignored_missing_anchors=frozenset({("guide/index.html", "known")}),
  ) == ["index.html: missing anchor 'unexpected' in guide/index.html"]


def test_check_site_resolves_fragment_only_links_in_flat_pages(tmp_path):
  write_site(tmp_path)
  (tmp_path / "404.html").write_text(
    '<h1 id="top">Not found</h1>'
    '<a href="#top">Top</a>'
    '<a href="#absent">Broken</a>',
    encoding="utf-8",
  )

  assert cdl.check_site(tmp_path) == [
    "404.html: missing anchor 'absent' in 404.html"
  ]


def test_check_site_rejects_target_outside_site(tmp_path):
  site = tmp_path / "site"
  site.mkdir()
  (site / "index.html").write_text(
    '<a href="../private.txt">Escape</a>', encoding="utf-8"
  )
  (tmp_path / "private.txt").write_text("private", encoding="utf-8")

  assert cdl.check_site(site) == [
    "index.html: local target escapes the site: ../private.txt"
  ]


def test_check_site_requires_an_index(tmp_path):
  assert cdl.check_site(tmp_path) == [
    f"documentation site has no index: {tmp_path / 'index.html'}"
  ]
