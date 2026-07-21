"""Tests for repository branding assets and their public integrations."""

from __future__ import annotations

import xml.etree.ElementTree as ET
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "docs" / "assets"


def read(path: str) -> str:
  return (ROOT / path).read_text(encoding="utf-8")


def contrast_ratio(foreground: str, background: str) -> float:
  def luminance(color: str) -> float:
    channels = [
      int(color[offset:offset + 2], 16) / 255
      for offset in (1, 3, 5)
    ]
    linear = [
      channel / 12.92
      if channel <= 0.04045
      else ((channel + 0.055) / 1.055) ** 2.4
      for channel in channels
    ]
    return 0.2126 * linear[0] + 0.7152 * linear[1] + 0.0722 * linear[2]

  first = luminance(foreground)
  second = luminance(background)
  lighter, darker = max(first, second), min(first, second)
  return (lighter + 0.05) / (darker + 0.05)


def test_brand_assets_are_self_contained_vector_artwork():
  for name in (
    "tess-logo.svg",
    "tess-logo-dark.svg",
    "tess-symbol.svg",
    "tess-symbol-header.svg",
  ):
    root = ET.parse(ASSETS / name).getroot()
    assert root.tag == "{http://www.w3.org/2000/svg}svg"
    assert not list(root.iter("{http://www.w3.org/2000/svg}text"))

    for element in root.iter():
      href = element.attrib.get("href", "")
      assert not href or href.startswith("#")

  for name in ("tess-logo.svg", "tess-logo-dark.svg"):
    root = ET.parse(ASSETS / name).getroot()
    assert root.attrib["width"] == "396"
    assert root.attrib["height"] == "104"

  symbol = ET.parse(ASSETS / "tess-symbol.svg").getroot()
  assert symbol.attrib["width"] == "52"
  assert symbol.attrib["height"] == "52"


def test_logo_font_provenance_is_publicly_documented():
  dependencies = read("docs/dependencies.md")
  assert "Sirenia Light" in dependencies
  assert "https://fonts.adobe.com/fonts/sirenia" in dependencies
  assert (
    "https://helpx.adobe.com/fonts/using/font-licensing.html"
    in dependencies
  )
  assert "does not distribute the font software" in dependencies


def test_readme_and_docs_home_use_theme_appropriate_lockups():
  readme = read("README.md")
  assert "tess-logo.svg" in readme
  assert "tess-logo-dark.svg" in readme
  assert "prefers-color-scheme: dark" in readme

  docs_home = read("docs/index.md")
  assert "tess-logo.svg#only-light" in docs_home
  assert "tess-logo-dark.svg#only-dark" in docs_home


def test_docs_heading_font_is_vendored_with_license():
  docs_css = read("docs/stylesheets/extra.css")
  assert "fraunces-latin.woff2" in docs_css
  assert 'font-family: "Fraunces"' in docs_css

  assert (ASSETS / "fonts" / "fraunces-latin.woff2").is_file()
  license_text = read("docs/assets/fonts/OFL-fraunces.txt")
  assert "SIL Open Font License" in license_text


def test_mkdocs_uses_compact_symbol_for_navigation_and_favicon():
  config = read("mkdocs.yml")
  assert "logo: assets/tess-symbol-header.svg" in config
  assert "favicon: assets/tess-symbol.svg" in config


def test_branding_controls_meet_non_text_contrast_minimum():
  css = read("examples/web_pathfinder/site/style.css")
  assert "border: 1px solid #75658f" in css
  assert contrast_ratio("#75658f", "#171323") >= 3

  header_symbol = read("docs/assets/tess-symbol-header.svg")
  assert 'fill="#17131f"' in header_symbol
  assert 'stop-color="#705eb1"' in header_symbol
  assert contrast_ratio("#17131f", "#7e56c2") >= 3
  assert contrast_ratio("#705eb1", "#17131f") >= 3


def test_web_demo_uses_brand_logo_and_compact_favicon():
  html = read("examples/web_pathfinder/site/index.html")
  favicon = read("examples/web_pathfinder/site/favicon.svg")
  build_script = read("tools/build_web_demo.sh")

  assert 'class="brand"' in html
  assert 'src="logo.svg"' in html
  assert "tess-logo-dark.svg" in build_script
  assert "Raised destination tile" in favicon


def test_doxygen_uses_the_compact_symbol():
  cmake = read("CMakeLists.txt")
  assert "DOXYGEN_PROJECT_LOGO" in cmake
  assert "docs/assets/tess-symbol.svg" in cmake
