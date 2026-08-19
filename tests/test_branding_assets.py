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


def test_site_root_favicon_is_a_multi_size_ico():
  import struct

  data = (ROOT / "docs" / "favicon.ico").read_bytes()
  reserved, image_type, count = struct.unpack("<HHH", data[:6])
  assert (reserved, image_type) == (0, 1)
  assert count >= 3

  sizes = set()
  for index in range(count):
    entry = data[6 + 16 * index : 6 + 16 * (index + 1)]
    width, offset = entry[0], struct.unpack("<I", entry[12:16])[0]
    sizes.add(width)
    assert data[offset : offset + 8] == b"\x89PNG\r\n\x1a\n"
  assert {16, 32, 48} <= sizes


def test_social_preview_matches_github_card_dimensions():
  import struct

  data = (ASSETS / "tess-social-preview.png").read_bytes()
  assert data[:8] == b"\x89PNG\r\n\x1a\n"
  width, height = struct.unpack(">II", data[16:24])
  assert (width, height) == (2560, 1280)
  assert len(data) < 1024 * 1024  # GitHub's social-preview upload limit.


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


def test_docs_search_metadata_establishes_tess_site_identity():
  config = read("mkdocs.yml")
  readme = read("README.md")
  robots = read("docs/robots.txt")
  template = read("overrides/main.html")

  assert (
    "site_description: >-\n"
    "  Header-only C++20 library for grid pathfinding, tile-world storage, "
    "and\n"
    "  deterministic simulation"
    in config
  )
  expected_titles = {
    "docs/index.md": "C++20 Grid Pathfinding and Simulation Library",
    "docs/getting-started.md": (
      "Getting Started with C++20 Grid Pathfinding"
    ),
    "docs/examples.md": "C++20 Grid Pathfinding and Simulation Examples",
    "docs/guide/pathfinding.md": (
      "C++ Grid Pathfinding Strategies for Game Workloads"
    ),
    "docs/performance.md": "C++ Grid Pathfinding and Simulation Benchmarks",
    "docs/packaging.md": "Install the tess Header-Only C++20 Library",
    "docs/pathfinding-strategy-comparison.md": (
      "A* vs Route Caches, Batches, and Distance Fields"
    ),
    "docs/use-cases.md": "Grid Pathfinding and Simulation Use Cases",
  }
  for path, title in expected_titles.items():
    assert read(path).startswith(
      f"---\ntitle: {title}\ndescription: >-\n"
    )

  assert readme.startswith("<p align=\"center\">")
  assert (
    "# Pathfinding and Simulation for 2D and 3D Grid Worlds" in readme
  )
  assert (
    "`tess` is a fast, header-only C++20 library for pathfinding and "
    "deterministic\n"
    "simulation in 2D and 3D grid worlds"
    in readme
  )
  assert robots == (
    "User-agent: *\n"
    "Allow: /\n"
    "Sitemap: https://tess.owx.dev/sitemap.xml\n"
  )

  assert 'property="og:site_name" content="{{ config.site_name }}"' in template
  assert '"@type": "WebSite"' in template
  assert '"name": {{ config.site_name | tojson }}' in template
  assert '"alternateName": "tess.owx.dev"' in template
  assert '"url": {{ config.site_url | tojson }}' in template
  assert "if page and page.is_homepage" in template


def test_docs_lazy_loads_the_self_hosted_mermaid_runtime():
  template = read("overrides/main.html")

  assert (
    '<script src="{{ \'assets/javascripts/mermaid.min.js\' | url }}">'
    not in template
  )
  assert "const mermaidRuntimeUrl =" in template
  assert "document.baseURI," in template
  assert ").href;" in template
  assert "window.mermaid = mermaidProxy;" in template
  assert 'document.createElement("script")' in template
  assert "script.src = mermaidRuntimeUrl;" in template
  assert "mermaidRuntime.initialize(initializationOptions);" in template
  assert "return mermaidRuntime.render(...args);" in template
  assert "unpkg.com" not in template


def test_docs_theme_overrides_preserve_accessible_names_and_contrast():
  template = read("overrides/main.html")
  css = read("docs/stylesheets/extra.css")
  config = read("mkdocs.yml")

  assert "querySelector(" in template
  assert '.md-search[role=\\"dialog\\"]' in template
  assert "setAttribute(" in template
  assert '"aria-label", {{ lang.t("search") | tojson }}' in template

  assert "opacity: 0.85" not in css
  assert "text-decoration: underline" in css
  assert ".md-typeset a:not(.md-button):not(.headerlink)" in css
  assert ".tess-hero .md-button:not(.md-button--primary)" in css
  assert ".tess-version a:is(:hover, :focus)" in css
  assert (
    ".md-button:not(.md-button--primary):is(:hover, :focus)" in css
  )
  assert "background-color: #512da8" in css
  assert "background-color: #d1b3ff" in css
  contrast_pairs = (
    ("#3f3f46", "#ffffff"),
    ("#512da8", "#ffffff"),
    ("#4527a0", "#ede7f6"),
    ("#e2e4e9", "#1e2129"),
    ("#d1b3ff", "#1e2129"),
    ("#ffffff", "#4527a0"),
    ("#455a64", "#f5f5f5"),
    ("#b0bec5", "#272a35"),
    ("#ff8a80", "#272a35"),
    ("#b39ddb", "#272a35"),
    ("#ffffff", "#111317"),
  )
  for color in {color for pair in contrast_pairs for color in pair}:
    if color not in {"#1e2129", "#f5f5f5", "#272a35", "#111317"}:
      assert color in css
  for foreground, background in contrast_pairs:
    assert contrast_ratio(foreground, background) >= 4.5

  assert "Diagnostics guide: guide/diagnostics.md" in config
  assert "Diagnostics architecture: architecture/diagnostics.md" in config


def test_mkdocs_navigation_includes_persistence_architecture():
  config = read("mkdocs.yml")

  assert "Persistence: architecture/persistence.md" in config
  assert "      - Interactive comparison: pathfinding-strategy-comparison.md" in (
    config
  )
  assert "  - Pathfinding comparison: pathfinding-strategy-comparison.md" not in (
    config
  )


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


def test_strategy_demo_uses_shared_cpp_results_and_accessible_embed():
  model = read("examples/pathfinding_strategies_model.cc")
  model_header = read("examples/pathfinding_strategies_model.h")
  native = read("examples/pathfinding_strategies.cc")
  wasm = read("examples/web_pathfinding_strategies/strategies_wasm.cc")
  html = read("examples/web_pathfinding_strategies/site/index.html")
  app = read("examples/web_pathfinding_strategies/site/app.js")
  css = read("examples/web_pathfinding_strategies/site/style.css")
  site_css = read("docs/stylesheets/extra.css")
  build_script = read("tools/build_web_demo.sh")
  cmake = read("examples/CMakeLists.txt")
  article = read("docs/pathfinding-strategy-comparison.md")
  pages = read(".github/workflows/pages.yml")

  for call in (
    "astar_path",
    "cached_astar_path",
    "weighted_path_batch",
    "build_distance_field",
    "distance_field_path",
  ):
    assert call in model
    assert call not in wasm

  assert "std::array<PathPoint, path_capacity>" in model_header
  assert "std::optional" in model_header
  assert "enum class StrategyKind : std::uint8_t" in model_header
  assert "enum class BrowserPathStatus : std::uint8_t" in model_header
  assert "static_cast<std::size_t>(y) *" in model
  assert "verify_copied_paths_after_scratch_reuse" in native
  assert "verify_obstacle_map" in native
  assert "verify_invalid_checked_reads" in native
  assert "same_copied_path" in model
  assert "pathfinding_strategies_model.cc" in cmake
  assert "tess_pathfinding_strategies_wasm_adapter OBJECT" in cmake
  assert "web_pathfinding_strategies/strategies_wasm.cc" in cmake
  assert "pathfinding_strategies_model.cc" in build_script
  assert "strategies_wasm.cc" in build_script

  assert "std::int32_t" in wasm
  assert "kInvalid = -1" in wasm
  assert "size_t" not in wasm
  assert "PathStatus" not in wasm
  assert "tess_strategies_cell_passable" in wasm
  assert "_tess_strategies_cell_passable" in build_script
  assert "tess_strategies_field_reached_nodes" in wasm
  assert "_tess_strategies_field_reached_nodes" in build_script
  assert "dataset.tessStrategies" in app
  assert 'data-tess-strategies="loading"' not in html
  assert 'dataset.tessStrategies = "ready"' in app
  assert 'dataset.tessStrategies = "failed"' in app
  assert "async function initializeStrategies()" in app
  assert "await createTessStrategies()" in app
  assert "void initializeStrategies();" in app
  assert "validateSnapshot" in app
  assert "Cache hits: 1" in app
  assert "Cache misses: 1" in app
  assert "Field builds: 1" in app
  assert "A* fallbacks: 0" in app
  assert "api.cellPassable" in app
  assert "api.fieldReachedNodes" in app
  assert 'tile.classList.add("blocked")' in app
  assert 'tile.classList.toggle("field-covered"' in app

  assert 'id="strategy-results"></tbody>' in html
  assert 'aria-live="polite"' in html
  assert 'aria-pressed="false"' in html
  assert 'id="replay" type="button" disabled' in html
  assert 'id="pause" type="button" aria-pressed="false" disabled' in html
  assert 'root.dataset.tessStrategies !== "ready"' in app
  assert "replay.disabled = false" in app
  assert 'class="tile-grid"' in html
  assert "Comparable requests intentionally match" in html
  assert "The routes intentionally match" not in html
  for topology in (
    'data-topology="astar"',
    'data-topology="cache"',
    'data-topology="batch"',
    'data-topology="field"',
  ):
    assert topology in html
  assert 'aria-label="Call topology"' in html
  assert "updateTopology" in app
  assert "fieldRevealCounts" in app
  assert "animationStage >= 3" in app
  assert "pause.hidden = reducedMotion.matches" in app
  assert "topology-step" in css
  assert "prefers-reduced-motion" in app
  assert "overflow-x: hidden" in css
  assert "@media (max-width: 520px)" in css
  assert ".strategy-grid { grid-template-columns: 1fr; }" in css

  assert 'class="strategy-demo-frame"' in article
  assert 'src="../demo/strategies/"' in article
  assert 'loading="lazy"' in article
  assert 'title="Interactive pathfinding strategy comparison"' in article
  assert "comparable requests return the same routes" in article
  assert "returned routes are intentionally identical across cards" not in article
  assert "Open the strategy demo in a separate page" in article
  assert "[complete self-checking example][strategy-main]" in article
  assert "[strategy-main]:" in article
  assert "## Algorithm and strategy status" in article
  assert "four peer algorithms" in article
  assert "Unit-cost A\\* and weighted A\\*" in article
  assert '<div class="strategy-status-table" markdown="1">' in article
  assert "@media (max-width: 40rem)" in site_css
  assert ".strategy-status-table tr" in site_css
  assert ".strategy-status-table thead" in site_css
  for status_class in (
    "strategy-status--released",
    "strategy-status--designed",
    "strategy-status--boundary",
    "strategy-status--rejected",
  ):
    assert status_class in article
    assert f".{status_class}" in site_css
  assert '<details class="strategy-alternatives" markdown="1">' in article
  assert "<summary>Why some alternatives were not promoted</summary>" in article
  assert ".strategy-alternatives" in site_css
  assert "JPS, bidirectional A\\*, Theta\\*, and D\\* Lite" in article
  assert "no maintained roadmap or" in article
  assert "rejection decision in this repository" in article
  assert "2026-08-10-quad-heap-rejected.md" in article
  assert "2026-08-17-colony-balanced-waypoints-rejected.md" in article
  assert "2026-08-17-colony-wide-merge-second-wave.md" in article
  assert "planning/local-movement-resolution.md" in article

  assert "demo/strategies/" in pages
  strategy_smoke = pages.split("demo/strategies/", maxsplit=1)[1]
  assert "wait_for_browser_state.py" in strategy_smoke
  assert "data-cache-hits=\"1\"" in strategy_smoke
  assert "data-cache-misses=\"1\"" in strategy_smoke
  assert "data-batch-field-builds=\"1\"" in strategy_smoke
  assert "data-batch-fallbacks=\"0\"" in strategy_smoke
  assert "data-field-reached-nodes=\"211\"" in strategy_smoke
  assert "data-blocked-cells-per-grid=\"45\"" in strategy_smoke
  assert "data-distance-field-covered-cells=\"211\"" in strategy_smoke
  assert "data-distance-field-third-route=\"ready\"" in strategy_smoke


def test_colony_demo_reports_wall_and_crowd_blocked_outcomes():
  model = read("examples/web_colony/colony_model.cc") + read(
    "examples/web_colony/colony_model_internal.h"
  )
  model_header = read("examples/web_colony/colony_model.h")
  wasm = read("examples/web_colony/colony_wasm.cc")
  native = read("examples/web_colony/colony_native.cc")
  html = read("examples/web_colony/site/index.html")
  app = read("examples/web_colony/site/app.js")
  build_script = read("tools/build_web_demo.sh")
  cmake = read("examples/CMakeLists.txt")

  assert not (ROOT / "examples/web_colony/colony.cc").exists()
  for source in ("colony_model.cc", "colony_native.cc"):
    assert source in cmake
  native_target = cmake.split("tess_web_colony_model", 1)[1].split(")", 1)[0]
  assert "colony_wasm.cc" not in native_target
  assert "tess_web_colony_wasm_adapter OBJECT" in cmake
  assert "web_colony/colony_wasm.cc" in cmake
  assert "colony_model.cc" in build_script
  assert "colony_wasm.cc" in build_script

  # The page distinguishes bounded planning backlog from agents that already
  # have routes but could not move on the latest fixed tick.
  for metric in (
    "planning_pending",
    "advanced_last_tick",
    "movement_waits_last_tick",
  ):
    assert metric in model_header
  for export in (
    "tess_colony_planning_pending",
    "tess_colony_advanced_last_tick",
    "tess_colony_movement_waits_last_tick",
  ):
    assert export in wasm
    assert f"_{export}" in build_script
  assert "api.planningPending()" in app
  assert "api.advancedLastTick()" in app
  assert "api.movementWaitsLastTick()" in app
  assert "pending plans" in app
  assert "movement waits" in app

  # Congestion spreading is an explicit browser policy. It activates only
  # after measured movement waits and retains protected access aisles around
  # the separated endpoint columns.
  assert "set_spread_congested_routes" in model_header
  assert "tess_colony_set_spread" in wasm
  assert "_tess_colony_set_spread" in build_script
  assert "api.setSpread(spread.checked ? 1 : 0)" in app
  assert 'id="spread" type="checkbox" checked' in html
  assert "const bandWidth = 18" in app
  assert "kEndpointBandWidth = 18" in model
  assert "update_route_diversity" in model
  assert "endpoint_approach_obstructed" in model

  assert "tess_colony_unreachable" in wasm
  assert "_tess_colony_unreachable" in build_script
  assert "api.unreachable()" in app
  assert "wall-blocked" in app

  # A pointer stroke cannot make an occupied source impassable. The backend
  # decides admission, and JavaScript persists only requests it accepted.
  assert "world.field<OccupancyTag>(coord)" in model
  assert "occupied wall request was accepted" in native
  assert "set_wall(int x, int y, bool built)" in model_header
  assert "api.setWall(x, y, built ? 1 : 0)" in app
  assert "const rememberedWalls = Array.from(walls)" in app
  assert "api.setWall(key % width, Math.floor(key / width), 1) === 1" in app
  assert "strokeBuilt = !walls.has" in app
  assert "paintLine(lastCell, at, strokeBuilt)" in app
  assert "Drag open space to draw; drag from a wall to erase." in html
  assert "wall removal did not recover route" in native
  assert "idempotent add rebuilt topology" in native

  # A durable verdict is decided by search, not by a retry clock, so the
  # page cannot report a merely congested convoy as permanently stuck. Three
  # checks back it: the terrain graph rejects a sealed goal cheaply, then a
  # Traveler search asks whether settled colonists leave a route, and a Walker
  # search confirms whether a Traveler-only failure is durable terrain or
  # crowd-blocked for one leg. A bounded, jittered recovery schedule controls
  # when those exact verdicts are requested; delay replaces no semantics.
  assert "recover_blocked_agents" in model
  assert "precheck_path<Walker>" in model
  assert "astar_path<World, Traveler>" in model
  assert "astar_path<World, Walker>" in model
  assert "kRecoveryWindowTicks" in model
  assert "BlockedAgentRecoverySchedule" in model
  assert (
    "2U * static_cast<std::uint32_t>(demo->agents.size()) + 8U"
    not in model
  )

  assert "tess_colony_crowd_blocked" in wasm
  assert "_tess_colony_crowd_blocked" in build_script
  assert "api.crowdBlocked()" in app
  assert "tess_colony_turnaround_ready" in wasm
  assert "_tess_colony_turnaround_ready" in build_script
  assert "api.turnaroundReady()" in app
  assert "crowd-blocked agents waiting for turnaround" in app
  assert "tess_colony_completed_legs" in wasm
  assert "_tess_colony_completed_legs" in build_script
  assert "api.completedLegs()" in app
  assert "tess_colony_aborted_legs" in wasm
  assert "_tess_colony_aborted_legs" in build_script
  assert "api.abortedLegs()" in app

  # Agents quiescent for the leg are obstacles to everyone else, or a
  # bottleneck routes through colonists already standing at their goals.
  assert "SettledTag" in model
  assert "using Traveler" in model
  assert "World, Traveler, kMaxCost, OccupancyTag, ReservationTag" in model

  # Traveler reads SettledTag, so settling a colonist changes passability and
  # must move the chunk's content version -- the unit route cache invalidates
  # on that version and a plain field write does not touch it. Without the
  # bump a replan is served the cached route through the tile that just
  # closed, and the agent retries it forever.
  assert "mark_content_changed" in model
  assert "kSettledDirty" not in model

  # A colony can stop dead with nobody durably blocked -- two agents each
  # standing on the tile the other needs. Reporting only the durable count
  # left that reading as a running colony over a frozen grid, so sustained
  # absence of movement is reported too, and the page must keep saying
  # "Colony running" in the ordinary case for the Pages smoke.
  assert "tess_colony_stalled_ticks" in wasm
  assert "_tess_colony_stalled_ticks" in build_script
  assert "api.stalledTicks()" in app
  assert "Colony stalled" in app
  assert "'Colony running'" in app

  # Fixed-tick state stays in C++; interpolation is presentation-only. The
  # browser reads the previous/current pair and the accumulator alpha, then
  # lerps without feeding fractional positions back into tess.
  assert "previous_agent_xy" in model
  assert "interpolation_alpha" in model
  assert "tess_colony_previous_agents" in wasm
  assert "tess_colony_interpolation_alpha" in wasm
  assert "_tess_colony_previous_agents" in build_script
  assert "_tess_colony_interpolation_alpha" in build_script
  assert "api.previousAgents()" in app
  assert "api.interpolationAlpha()" in app
  assert "previous + (current - previous) * alpha" in app
  assert "swap animation pair diverged" in native
  assert "const agentSize = cell * 0.72" in app
  assert "canvas render excluded" in app

  # Tutorial structure is part of the example contract: the model labels the
  # reusable library patterns and repairs a rejected delta stream by publishing
  # a full baseline before applying more incremental frames.
  assert "Example: queue a world edit" in model
  assert "Example: rebuild derived topology on dirty input" in model
  assert "Example: run bounded pathing before movement" in model
  assert "Example: recover a rejected DeltaFrame" in model
  assert "collect_baseline" in model
  assert "shadow[shadow_index] != 0" in native
  assert "version != impl().deltas.version()" in native
  assert "class ColonyModel" in model_header
  assert "run_native_self_check" not in model_header
  assert "run_native_self_check" in native


def test_traffic_lab_has_large_grid_diagnostics_and_browser_evidence():
  header = read("examples/web_traffic/traffic_model.h")
  model = read("examples/web_traffic/traffic_model.cc")
  native = read("examples/web_traffic/traffic_native.cc")
  wasm = read("examples/web_traffic/traffic_wasm.cc")
  html = read("examples/web_traffic/site/index.html")
  app = read("examples/web_traffic/site/app.js")
  style = read("examples/web_traffic/site/style.css")
  cmake = read("examples/CMakeLists.txt")
  build = read("tools/build_web_demo.sh")
  pages = read(".github/workflows/pages.yml")
  browser_test = read("tools/test_web_demo_interactions.py")
  measurement = read("tools/measure_traffic_lab.py")

  assert "traffic_width = 1024" in header
  assert "traffic_height = 512" in header
  assert "traffic_agents = 1024" in header
  for scenario in ("Aligned", "ShuffledCrossing", "Funnel", "MultiGate"):
    assert scenario in header
  for name in ("aligned", "shuffled-crossing", "funnel", "multi-gate"):
    assert name in native
  assert "kSearchBudget = 8" in model
  assert "first.max_planning_queries() != 8" in native
  assert "scenario is not deterministic" in native
  assert "estimated_memory_bytes" in header
  assert "planning_queries_last_tick" in header
  assert "planning_touched_nodes_last_tick" in header
  assert '"--samples"' in native
  assert '"--profile-repetitions"' in native

  # Measurement-grade timing stays in the example harness. The native tool
  # emits one fixed-tick row, while the campaign runner owns fresh processes,
  # percentile sample floors, and advisory artifacts.
  assert "PERCENTILE_MINIMUMS" in measurement
  assert '"p99": 2000' in measurement
  assert "subprocess.run" in measurement
  assert '"authority": "advisory"' in measurement

  assert "tess_web_traffic_model" in cmake
  assert "tess_web_traffic_diagnostics_model" in cmake
  assert "TESS_ENABLE_DIAGNOSTICS" in cmake
  assert "tess_web_traffic_wasm_adapter" in cmake
  assert "traffic_model.cc" in build
  assert "traffic_wasm.cc" in build
  assert "createTessTraffic" in build
  for metric in (
    "planning_us",
    "planning_queries",
    "fixed_ticks",
    "planning_pending",
    "advanced",
    "waits",
    "blocked",
    "one_progress_streak",
    "longest_one_progress_streak",
  ):
    assert f"tess_traffic_{metric}" in wasm
    assert f"_tess_traffic_{metric}" in build

  assert 'width="1024" height="512"' in html
  assert "aspect-ratio: 2 / 1" in style
  assert "const terrainLayer = document.createElement('canvas')" in app
  assert "terrainCtx.putImageData" in app
  assert "ctx.drawImage(terrainLayer" in app
  for label in (
    "C++ update",
    "planning",
    "render",
    "frame",
    "waits",
    "blocked",
    "one-agent streak",
  ):
    assert label in app

  assert "new URLSearchParams(window.location.search)" in app
  assert "measure=1" in app
  assert "Float64Array" in app
  assert "if (this.count === this.capacity)" in app
  assert "% this.capacity" not in app
  assert "maximum: values.length > 0" in app
  assert "window.tessTrafficMetrics" in app
  assert "fixedTicksLastCall" in app
  assert "Snapshot latency" in html
  assert "measurement-output" in app

  assert "tools/test_web_demo_interactions.py" in pages
  assert "open-cell click did not add a wall" in browser_test
  assert "draw stroke changed mode" in browser_test
  assert "fast drag did not interpolate" in browser_test
  assert "1366, 768" in browser_test
  assert "1920, 1080" in browser_test
  assert "Traffic Lab measurement mode did not collect samples" in browser_test
  assert "Traffic Lab measurement snapshot was not readable" in browser_test


def test_traffic_lab_self_checks_are_sliced_by_scenario():
  native = read("examples/web_traffic/traffic_native.cc")
  cmake = read("examples/CMakeLists.txt")

  assert '"--self-check-catalog"' in native
  assert '"--self-check"' in native
  for name in (
    "tess_web_traffic_catalog",
    "tess_web_traffic_aligned",
    "tess_web_traffic_shuffled_crossing",
    "tess_web_traffic_funnel",
    "tess_web_traffic_multi_gate",
  ):
    assert f"NAME {name}" in cmake
  assert "tess_web_traffic_catalog PROPERTIES" in cmake
  assert "tess_web_traffic_aligned PROPERTIES" in cmake
  assert "tess_web_traffic_shuffled_crossing PROPERTIES" in cmake
  assert "tess_web_traffic_funnel PROPERTIES" in cmake
  assert "tess_web_traffic_multi_gate PROPERTIES" in cmake
  assert "TIMEOUT 600" in cmake
  assert "TIMEOUT 300" in cmake


def test_wasm_diagnostics_demo_has_real_accessible_integration_contract():
  build_script = read("tools/build_web_demo.sh")
  model = read("examples/web_diagnostics/diagnostics_model.cc")
  native = read("examples/web_diagnostics/diagnostics_native.cc")
  wasm = read("examples/web_diagnostics/diagnostics_wasm.cc")
  html = read("examples/web_diagnostics/site/index.html")
  app = read("examples/web_diagnostics/site/app.js")
  pages = read(".github/workflows/pages.yml")
  dependencies = read("docs/dependencies.md")

  revision = "8936b58fe26e8c3da834b8f60b06511d537b4c63"
  assert revision in build_script
  assert revision in dependencies
  assert "cmake/TessGitPopulate.cmake" in build_script
  assert "TESS_GIT_REVISION" in build_script
  for source in (
    "imgui.cpp",
    "imgui_draw.cpp",
    "imgui_tables.cpp",
    "imgui_widgets.cpp",
    "imgui_impl_glfw.cpp",
    "imgui_impl_opengl3.cpp",
  ):
    assert source in build_script
  assert "imgui_demo.cpp" not in build_script
  assert "-DTESS_ENABLE_DIAGNOSTICS" in build_script
  assert "-DTESS_ENABLE_IMGUI" in build_script
  assert "-sUSE_GLFW=3" in build_script
  assert "-sMIN_WEBGL_VERSION=2" in build_script
  assert "-sMAX_WEBGL_VERSION=2" in build_script
  assert 'LICENSE.txt" "$diagnostics/third-party-imgui-LICENSE.txt' in (
    build_script
  )

  for control in (
    'id="paused"',
    'id="intensity"',
    'id="selected-x"',
    'id="selected-y"',
    'id="passable"',
  ):
    assert control in html
  assert 'aria-live="polite"' in html
  assert "third-party-imgui-LICENSE.txt" in html

  assert 'dataset.tessDiagnostics = "ready"' in app
  assert 'dataset.tessDiagnostics = "failed"' in app
  assert "verificationTimeoutMs" in app
  for export in (
    "tess_diagnostics_status",
    "tess_diagnostics_set_paused",
    "tess_diagnostics_set_intensity",
    "tess_diagnostics_select",
    "tess_diagnostics_set_passable",
    "tess_diagnostics_paused",
    "tess_diagnostics_intensity",
    "tess_diagnostics_selected_x",
    "tess_diagnostics_selected_y",
    "tess_diagnostics_selected_passable",
  ):
    assert export in app
    assert f"_{export}" in build_script

  assert "ScopedTimer" in model
  assert '"path_search"' in model
  assert '"queued_phase"' in model
  assert "consumer-instrumented" in wasm
  assert "ImGui::GetDrawData()->TotalVtxCount" in wasm
  assert "draw_diagnostics_panel" in wasm
  assert "draw_world_overview" in wasm
  assert "draw_chunk_inspector" in wasm
  assert "draw_bool_field_editor" in wasm
  assert "ImGui_ImplGlfw_InstallEmscriptenCallbacks" in wasm
  assert "readiness_without_each_required_signal_is_false" in native
  assert "expected_path_outcomes_remain_operational" in native
  assert "passable.checked = api.selectedPassable() === 1" in app
  assert "verifyMirroredControls" in app

  assert "demo/diagnostics/" in pages
  assert "--dataset tessDiagnostics" in pages
  assert "--expected ready" in pages
  assert "--use-angle=swiftshader" in pages
  assert "--enable-unsafe-swiftshader" in pages
  diagnostics_poll = pages.split("demo/diagnostics/", maxsplit=1)[1]
  assert "--disable-gpu" not in diagnostics_poll.split("Configure Pages", 1)[0]


def test_doxygen_uses_the_compact_symbol():
  cmake = read("CMakeLists.txt")
  assert "DOXYGEN_PROJECT_LOGO" in cmake
  assert "docs/assets/tess-symbol.svg" in cmake
