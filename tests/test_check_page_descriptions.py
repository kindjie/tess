"""Tests for the built-site description requirement."""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

import check_page_descriptions as cpd  # noqa: E402


def _page(description: str | None) -> str:
  meta = (
    f'<meta name="description" content="{description}">'
    if description is not None
    else ""
  )
  return f"<html><head>{meta}</head><body>x</body></html>"


def _make_site(root: Path) -> None:
  (root / "guide").mkdir(parents=True)
  (root / "api").mkdir()
  (root / "index.html").write_text(_page("The tess homepage."))
  (root / "guide" / "index.html").write_text(_page("A specific guide."))
  (root / "404.html").write_text(_page(cpd.GENERIC_PREFIX + " and more"))
  # Generated trees are out of scope here; their contract lives in
  # finalize_generated_pages.py.
  (root / "api" / "unstamped.html").write_text(_page(None))


def test_accepts_specific_descriptions_everywhere(tmp_path):
  _make_site(tmp_path)

  assert cpd.check_site(tmp_path) == []


def test_rejects_the_generic_fallback(tmp_path):
  _make_site(tmp_path)
  (tmp_path / "guide" / "index.html").write_text(
    _page(cpd.GENERIC_PREFIX + " and deterministic simulation")
  )

  failures = cpd.check_site(tmp_path)

  assert len(failures) == 1
  assert "generic" in failures[0]


def test_rejects_a_missing_or_duplicated_description(tmp_path):
  _make_site(tmp_path)
  (tmp_path / "guide" / "index.html").write_text(_page(None))
  (tmp_path / "extra.html").write_text(
    _page("One.").replace(
      "</head>", '<meta name="description" content="Two."></head>'
    )
  )

  failures = cpd.check_site(tmp_path)

  assert len(failures) == 2
  assert any("guide" in f and "one description" in f for f in failures)
  assert any("extra" in f for f in failures)


def test_fails_vacuously_empty_enumerations(tmp_path):
  (tmp_path / "api").mkdir(parents=True)
  (tmp_path / "api" / "x.html").write_text(_page(None))

  failures = cpd.check_site(tmp_path)

  assert failures == ["no maintained pages were checked; wrong directory?"]
