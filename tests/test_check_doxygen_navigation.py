"""Tests for generated Doxygen return-navigation validation."""

from __future__ import annotations

import importlib.util
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
  "check_doxygen_navigation",
  ROOT / "tools" / "check_doxygen_navigation.py",
)
assert SPEC is not None and SPEC.loader is not None
CHECK = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CHECK)


def write_api(root: Path, menu: str) -> Path:
  """Create the minimum generated API surface the checker accepts."""
  root.mkdir()
  (root / "index.html").write_text("<title>API</title>")
  (root / "annotated.html").write_text("<title>Classes</title>")
  (root / "menudata.js").write_text(menu)
  return root


VALID_MENU = """var menudata={children:[
{text:"Docs",url:"../"},
{text:"Learn",url:"../getting-started/"},
{text:"Reference",url:"../reference/"},
{text:"API home",url:"index.html"}]}
"""


def test_generated_navigation_resolves_at_every_public_prefix(tmp_path: Path):
  api = write_api(tmp_path / "api", VALID_MENU)

  assert CHECK.check_navigation(api) == []


def test_generated_navigation_rejects_missing_or_wrong_tabs(tmp_path: Path):
  wrong = VALID_MENU.replace('../reference/', '../refernece/')
  api = write_api(tmp_path / "api", wrong)

  failures = CHECK.check_navigation(api)

  assert failures == [
    "menudata.js: Reference points to '../refernece/', expected "
    "'../reference/'"
  ]


def test_generated_navigation_rejects_an_incomplete_api_tree(tmp_path: Path):
  api = tmp_path / "api"
  api.mkdir()
  (api / "menudata.js").write_text(VALID_MENU)

  assert CHECK.check_navigation(api) == [
    "generated API page is missing: index.html",
    "generated API page is missing: annotated.html",
  ]
