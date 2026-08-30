"""Contracts joining authored documentation to the Doxygen reference."""

from __future__ import annotations

import re
import xml.etree.ElementTree as ET
from pathlib import Path
from urllib.parse import urljoin


ROOT = Path(__file__).resolve().parents[1]
SITE = "https://tess.owx.dev/"

API_LINKS = {
  "tess::Shape": "structtess_1_1Shape.html",
  "tess::FieldSchema": "structtess_1_1FieldSchema.html",
  "tess::World": "classtess_1_1World.html",
  "tess::OperationBatch": "classtess_1_1OperationBatch.html",
  "tess::astar_path": (
    "classtess_1_1PathScratch.html"
    "#a7b7d735ab95ab0db2275b679188873b4"
  ),
  "tess::cached_astar_path": (
    "classtess_1_1PathScratch.html"
    "#aa95ff184a9767db00624e49b36b8d090"
  ),
  "tess::weighted_path_batch": (
    "classtess_1_1DistanceFieldScratch.html"
    "#a2578c1d6fe0cee9d288168f7d9821811"
  ),
  "tess::DistanceFieldProduct": (
    "classtess_1_1DistanceFieldProduct.html"
  ),
  "tess::FieldProductCache": "classtess_1_1FieldProductCache.html",
  "tess::RegionGraphT": "classtess_1_1RegionGraphT.html",
  "tess::Schedule": "classtess_1_1Schedule.html",
  "tess::DeltaFrame": "classtess_1_1DeltaFrame.html",
  "tess::movement::OverlayCost": (
    "structtess_1_1movement_1_1OverlayCost.html"
  ),
  "tess::PathAgentState": "structtess_1_1PathAgentState.html",
  "tess::PathAgentRoutes": "structtess_1_1PathAgentRoutes.html",
  "tess::PathAgentReplanQueue": (
    "classtess_1_1PathAgentReplanQueue.html"
  ),
}


def read(relative: str) -> str:
  """Read one repository file as UTF-8 text."""
  return (ROOT / relative).read_text(encoding="utf-8")


def test_doxygen_layout_returns_to_version_relative_authored_docs():
  """Every published prefix resolves the same supported return tabs."""
  layout = ROOT / "docs" / "doxygen-layout.xml"
  root = ET.parse(layout).getroot()
  tabs = {
    tab.attrib["title"]: tab.attrib["url"]
    for tab in root.findall("./navindex/tab[@type='user']")
  }

  assert tabs == {
    "Docs": "../",
    "Learn": "../getting-started/",
    "Reference": "../reference/",
  }
  cmake = read("CMakeLists.txt")
  dependencies = read("docs/dependencies.md")
  assert "find_package(Doxygen 1.17 REQUIRED)" in cmake
  assert "set(DOXYGEN_LAYOUT_FILE" in cmake
  assert 'docs/doxygen-layout.xml")' in cmake
  assert "Local builds require Doxygen 1.17.0 or newer" in dependencies

  page = "api/classtess_1_1_path_scratch.html"
  for prefix in ("", "main/", "0.13/", "1.0.0-rc.1/"):
    source = f"{SITE}{prefix}{page}"
    assert urljoin(source, tabs["Docs"]) == f"{SITE}{prefix}"
    assert urljoin(source, tabs["Learn"]) == (
      f"{SITE}{prefix}getting-started/"
    )
    assert urljoin(source, tabs["Reference"]) == (
      f"{SITE}{prefix}reference/"
    )


def test_doxygen_main_page_curates_supported_entry_points():
  """The API landing page routes by task without inventing modules."""
  main = read("docs/api-main.md")

  for heading in (
    "## Core worlds",
    "## Pathfinding",
    "## Simulation",
    "## Diagnostics",
    "## Optional integrations",
  ):
    assert heading in main
  for symbol in (
    "tess::Shape",
    "tess::FieldSchema",
    "tess::World",
    "tess::PathRequest",
    "tess::PathScratch",
    "tess::DistanceFieldProduct",
    "tess::OperationBatch",
    "tess::Schedule",
    "tess::DeltaFrame",
    "tess::diagnostics::DiagnosticsSnapshot",
    "tess::diagnostics::FlowAccounting",
    "tess::EnttTilePositionAdapter",
    "tess::FlecsTilePositionAdapter",
    "tess::gpu::WebGpuBackend",
  ):
    target = re.escape(symbol)
    inline = re.search(rf"\[`{target}`\]\(@ref\s+{target}\)", main)
    reference = re.search(rf"\[`{target}`\]\[([^]]+)]", main)
    if inline is None:
      assert reference is not None
      assert f"[{reference.group(1)}]: @ref {symbol}" in main


def test_stable_learning_pages_link_their_first_api_summary():
  """Learning pages lead with compact, version-local API entry points."""
  expected = {
    "docs/getting-started.md": (
      "tess::Shape",
      "tess::FieldSchema",
      "tess::World",
      "tess::OperationBatch",
      "tess::astar_path",
      "tess::RegionGraphT",
      "tess::Schedule",
      "tess::DeltaFrame",
    ),
    "docs/guide/pathfinding.md": (
      "tess::astar_path",
      "tess::cached_astar_path",
      "tess::weighted_path_batch",
      "tess::DistanceFieldProduct",
      "tess::FieldProductCache",
    ),
    "docs/pathfinding-strategy-comparison.md": (
      "tess::astar_path",
      "tess::cached_astar_path",
      "tess::weighted_path_batch",
      "tess::DistanceFieldProduct",
      "tess::FieldProductCache",
    ),
    "docs/guide/congestion.md": (
      "tess::movement::OverlayCost",
      "tess::PathAgentState",
      "tess::PathAgentRoutes",
      "tess::PathAgentReplanQueue",
    ),
  }

  for relative, symbols in expected.items():
    page = read(relative)
    first_section = page.index("\n## ")
    box = page.index('!!! info "API used"')
    assert box < first_section
    for symbol in symbols:
      match = re.search(
        rf"\[`{re.escape(symbol)}`\]\[([^]]+)]", page[:first_section]
      )
      assert match is not None, f"{relative}: {symbol} is not linked"
      definition = f"[{match.group(1)}]: {SITE}api/{API_LINKS[symbol]}"
      assert definition in page


def test_no_automatic_symbol_linking_or_unified_search_is_introduced():
  """Explicit links remain the only authored-to-generated integration."""
  cmake = read("CMakeLists.txt")
  tools = "\n".join(path.name for path in (ROOT / "tools").iterdir())

  assert "DOXYGEN_AUTOLINK_SUPPORT" not in cmake
  assert "symbol_map" not in tools
  assert "unified_search" not in tools


def test_pages_checks_generated_navigation_without_breaking_old_tags():
  """Current sources get the new check; historical rebuilds remain valid."""
  workflow = read(".github/workflows/pages.yml")
  build_job = workflow.split("  build:\n", 1)[1].split(
    "  deploy:\n", 1
  )[0]
  build = "cmake --build build/docs-api --target tess_docs"
  check = re.compile(
    r"build/publication/tools/check_doxygen_navigation\.py\s+\\?\s*"
    r"build/docs-api/docs/html"
  )

  assert "if [ -f docs/doxygen-layout.xml ]; then" in build_job
  assert "DOXYGEN_VERSION: 1.17.0" in workflow
  match = check.search(build_job)
  assert match is not None
  assert build_job.index(build) < match.start()
  assert match.end() < build_job.index(
    "cp -R build/docs-api/docs/html build/site/api"
  )
  ci = read(".github/workflows/ci.yml")
  assert "tests/test_check_doxygen_navigation.py" in ci
  assert "tests/test_doxygen_integration.py" in ci
