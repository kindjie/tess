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
  }
  for path, title in expected_titles.items():
    assert read(path).startswith(
      f"---\ntitle: {title}\ndescription: >-\n"
    )

  assert 'property="og:site_name" content="{{ config.site_name }}"' in template
  assert '"@type": "WebSite"' in template
  assert '"name": {{ config.site_name | tojson }}' in template
  assert '"alternateName": "tess.owx.dev"' in template
  assert '"url": {{ config.site_url | tojson }}' in template
  assert "if page and page.is_homepage" in template


def test_mkdocs_navigation_includes_persistence_architecture():
  config = read("mkdocs.yml")

  assert "Persistence: architecture/persistence.md" in config


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


def test_colony_demo_reports_wall_and_crowd_blocked_outcomes():
  model = read("examples/web_colony/colony_model.cc") + read(
    "examples/web_colony/colony_model_internal.h"
  )
  model_header = read("examples/web_colony/colony_model.h")
  wasm = read("examples/web_colony/colony_wasm.cc")
  native = read("examples/web_colony/colony_native.cc")
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

  assert "tess_colony_unreachable" in wasm
  assert "_tess_colony_unreachable" in build_script
  assert "api.unreachable()" in app
  assert "wall-blocked" in app

  # A pointer stroke cannot make an occupied source impassable. The backend
  # decides admission, and JavaScript persists only requests it accepted.
  assert "world.field<OccupancyTag>(coord)" in model
  assert "occupied wall request was accepted" in native
  assert "if (api.setWall(x, y) === 1)" in app

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
