"""Contract tests for documentation navigation and discovery."""

from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
  """Read one repository file as UTF-8 text."""
  return (ROOT / relative).read_text(encoding="utf-8")


def test_top_level_navigation_has_six_reader_facing_sections():
  """The navigation exposes exactly six stable reader-facing sections."""
  mkdocs = read("mkdocs.yml")
  nav = mkdocs.split("\nnav:\n", 1)[1].split("\nexclude_docs:", 1)[0]
  titles = re.findall(r"^  - ([^:\n]+):", nav, flags=re.MULTILINE)

  assert titles == [
    "Home",
    "Learn",
    "Guides",
    "Reference",
    "Performance",
    "Project",
  ]
  for expected in (
    "Getting started: getting-started.md",
    "Installation: packaging.md",
    "Tutorials: tutorials.md",
    "Examples: examples.md",
    "Reference: reference.md",
    "Architecture: architecture/README.md",
    "Compatibility evidence: architecture/compatibility.md",
    "API reference: https://tess.owx.dev/api/",
    "Performance overview: performance.md",
    "Strategy comparison: pathfinding-strategy-comparison.md",
    "Use cases: use-cases.md",
    "For agents: for-agents.md",
  ):
    assert expected in nav


def test_homepage_hero_has_three_actions_and_demo_cards_follow_it():
  """The hero stays focused while every live URL remains discoverable."""
  homepage = read("docs/index.md")
  hero = homepage.split('<div class="tess-hero" markdown>', 1)[1]
  hero, rest = hero.split("</div>", 1)

  actions = re.findall(r"\[([^]]+)]\(([^)]+)\)\{ \.md-button", hero)
  assert actions == [
    ("Get started", "getting-started.md"),
    ("Explore tutorials", "tutorials.md"),
    ("API reference", "api/"),
  ]
  assert "demo/" not in hero
  assert "## Live demonstrations" in rest
  for target in (
    "demo/",
    "demo/strategies/",
    "demo/colony/",
    "demo/congestion/",
    "demo/traffic/",
    "demo/tower/",
    "demo/diagnostics/",
    "demo/webgpu/",
  ):
    assert f"]({target})" in rest


def test_tutorial_and_example_catalogues_use_the_agreed_families():
  """Tutorial families and example modes remain explicit and distinct."""
  tutorials = read("docs/tutorials.md")
  examples = read("docs/examples.md")

  assert "# Tutorials" in tutorials
  for family in (
    "Colony simulation",
    "Pathfinding strategies",
    "Congestion pricing",
  ):
    assert family in tutorials
  for heading in (
    "## Guided tutorials",
    "## Interactive labs",
    "## Focused C++ recipes",
    "## Optional integrations",
  ):
    assert heading in examples
  assert "Congestion Lab" in examples
  assert "Traffic Lab" in examples
  assert "Tower" in examples
  assert "WebGPU" in examples


def test_pages_workflow_runs_responsive_homepage_browser_checks():
  """The assembled homepage is part of the publication browser smoke."""
  workflow = read(".github/workflows/pages.yml")
  interaction_tool = read("tools/test_web_demo_interactions.py")

  assert "--docs-url http://127.0.0.1:8000/" in workflow
  assert "test_docs_homepage(" in interaction_tool
  assert "prefers-reduced-motion" in interaction_tool
  assert "document.documentElement.scrollWidth <= innerWidth" in (
    interaction_tool
  )
