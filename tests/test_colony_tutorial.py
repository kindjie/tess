"""Contracts for the colony composition tutorial and article presentation."""

from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
  """Read one repository file as UTF-8 text."""
  return (ROOT / relative).read_text(encoding="utf-8")


def test_compiled_model_owns_every_tutorial_excerpt():
  """Every C++ excerpt is sourced from the compiled shared model."""
  model = read("examples/web_colony/colony_model.cc")
  tutorial = read("docs/tutorial/colony-composition.md")
  cmake = read("examples/CMakeLists.txt")

  regions = (
    "colony-queued-edits",
    "colony-schedule-order",
    "colony-delta-recovery",
  )
  for region in regions:
    assert model.count(f"// [{region}]") == 2
    marker = (
      f"<!-- tess-snippet: {region} "
      "source=examples/web_colony/colony_model.cc -->"
    )
    assert marker in tutorial

  assert "tess_web_colony_model" in cmake
  assert "subsystem:path;subsystem:sim" in cmake


def test_tutorial_teaches_the_existing_composition_and_boundaries():
  """The article explains the model without inventing another beginner path."""
  tutorial = read("docs/tutorial/colony-composition.md")
  nav = read("mkdocs.yml")
  index = read("docs/tutorials.md")
  examples = read("docs/examples.md")

  for phrase in (
    "queued world edits",
    "dirty-driven topology rebuild",
    "bounded path planning",
    "movement commit",
    "DeltaFrame",
    "presentation and recovery",
    "integer-tile",
    "browser-only",
    "128×128",
    "retained-route modes",
    "replan every tick",
  ):
    assert phrase in tutorial
  assert '!!! info "API used"' in tutorial
  assert 'class="colony-frame"' in tutorial
  assert 'src="../../demo/colony/?presentation=article"' in tutorial
  assert 'title="Interactive colony composition tutorial"' in tutorial
  assert "Open the full colony demo" in tutorial
  assert "Colony composition: tutorial/colony-composition.md" in nav
  assert "colony composition tutorial" in index.lower()
  assert "tutorial/colony-composition.md" in examples


def test_article_mode_is_responsive_accessible_and_presentation_only():
  """The same host offers a compact high-DPI surface for the article."""
  html = read("examples/web_colony/site/index.html")
  app = read("examples/web_colony/site/app.js")
  demo_styles = read("examples/web_colony/site/style.css")
  docs_styles = read("docs/stylesheets/extra.css")
  interactions = read("tools/test_web_demo_interactions.py")

  assert 'id="pause"' in html
  assert 'id="article-wall"' in html
  assert html.count('data-advanced-control="true"') >= 3
  assert html.count('aria-live="polite"') == 1
  assert html.count(" disabled") >= 7
  assert "get('presentation') === 'article'" in app
  assert "prefers-reduced-motion: reduce" in app
  assert "window.devicePixelRatio" in app
  assert "canvas.getBoundingClientRect().width" in app
  assert "function resizeCanvas" in app
  assert "function drawTileGrid" in app
  assert "function toggleArticleWall" in app
  assert "reducedMotion.addEventListener('change'" in app
  assert "advanceToTurnaround" in app
  assert "renderAlpha" in app
  assert "control.disabled = false" in app
  assert "dataset.tickUpdates" in app
  assert "article-mode" in demo_styles
  assert "data-advanced-control" in demo_styles
  assert ".colony-frame" in docs_styles
  assert "height:" in docs_styles
  assert "postMessage" not in app
  assert "ResizeObserver" not in app

  assert "def test_colony_article" in interactions
  assert "presentation=article" in interactions
  assert "prefers-reduced-motion" in interactions
  assert "Interactive colony composition tutorial" in interactions
  assert "frameOverflow" in interactions
  assert "noOverflow" in interactions


def test_ci_schedules_static_native_wasm_and_browser_checks():
  """Every verification layer is reachable from required workflows."""
  ci = read(".github/workflows/ci.yml")
  pages = read(".github/workflows/pages.yml")

  assert "tests/test_colony_tutorial.py" in ci
  assert "bash tools/build_web_demo.sh build/site/demo" in pages
  assert "python3 tools/test_web_demo_interactions.py" in pages
  assert "test -d docs/demo/colony" in pages
