"""Contracts for the flow field steering tutorial and live example."""

from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
  """Read one repository file as UTF-8 text."""
  return (ROOT / relative).read_text(encoding="utf-8")


def test_native_model_uses_public_distance_labels_deterministically():
  """The compiled model owns the tutorial's exact steering invariant."""
  header = read("examples/web_flow_steering/flow_steering_model.h")
  model = read("examples/web_flow_steering/flow_steering_model.cc")
  native = read("examples/web_flow_steering/flow_steering_native.cc")
  cmake = read("examples/CMakeLists.txt")

  assert "width = 32" in header
  assert "height = 24" in header
  assert "tess::DistanceFieldProduct" in model
  assert "distance_at<World>" in model
  assert "kDirectionOrder" in model
  assert "neighbor_distance == current_distance - 1" in model
  assert "tess::DistanceFieldProduct::unreachable_distance" in model
  assert "build_distance_field_product" in model
  assert "flow steering model: ok" in native
  for invariant in (
    "check_determinism",
    "check_legal_descent",
    "check_goal_and_unreachable_hold",
    "check_synchronous_rebuild",
  ):
    assert invariant in native

  assert "tess_web_flow_steering_model" in cmake
  assert "tess_web_flow_steering_wasm_adapter" in cmake
  assert "tess_web_flow_steering_self_check" in cmake
  assert "subsystem:path;subsystem:sim" in cmake


def test_wasm_host_exposes_state_without_reimplementing_steering():
  """The browser adapter is a narrow scalar view over the shared model."""
  wasm = read("examples/web_flow_steering/flow_steering_wasm.cc")
  build = read("tools/build_web_demo.sh")

  for name in (
    "reset",
    "tick",
    "set_goal",
    "goal_x",
    "goal_y",
    "agent_count",
    "agent_x",
    "agent_y",
    "agent_state",
    "tile_passable",
    "tile_distance",
  ):
    assert f"tess_flow_{name}" in wasm
    assert f'_tess_flow_{name}' in build
  assert "distance_at" not in wasm
  assert "web_flow_steering/flow_steering_model.cc" in build
  assert 'flow_steering="$output/flow-steering"' in build
  assert "createTessFlowSteering" in build


def test_tutorial_teaches_the_supported_boundary_and_embeds_accessibly():
  """The article explains the model and keeps adjacent concerns separate."""
  tutorial = read("docs/tutorial/flow-steering.md")
  terminology = read("docs/terminology.md")
  abbreviations = read("includes/abbreviations.md")
  model = read("examples/web_flow_steering/flow_steering_model.cc")
  styles = read("docs/stylesheets/extra.css")
  nav = read("mkdocs.yml")
  index = read("docs/tutorials.md")
  examples = read("docs/examples.md")
  docs_readme = read("docs/README.md")

  assert "title: Flow Field Steering from Distance Labels" in tutorial
  assert "Build flow field steering in tess" in tutorial
  assert "# Flow field steering from distance labels" in tutorial

  for phrase in (
    "distance labels",
    "retained direction field",
    "complete-path reconstruction",
    "deterministic tie-breaking",
    "dense",
    "entry-cost Bellman equality",
    "reservations",
    "congestion",
    "collision avoidance",
    "local steering",
  ):
    assert phrase in tutorial
  assert '!!! info "API used"' in tutorial
  assert (
    "https://tess.owx.dev/api/"
    "classtess_1_1DistanceFieldProduct.html" in tutorial
  )
  assert 'class="flow-steering-frame"' in tutorial
  assert 'src="../../demo/flow-steering/"' in tutorial
  assert 'title="Interactive flow field steering tutorial"' in tutorial
  assert "Open the flow field steering example" in tutorial
  assert ".flow-steering-frame" in styles
  assert "height:" in styles
  assert "Flow field steering: tutorial/flow-steering.md" in nav
  assert "## Flow field steering" in index
  assert "flow field steering tutorial" in index.lower()
  assert "**Flow field steering**" in examples
  assert "[Flow field steering]" in examples
  assert "flow field steering" in docs_readme.lower()
  assert "web_flow_steering" in examples
  assert "flow field steering" in terminology
  assert "flow field steering" in abbreviations
  assert "lifecycle flow accounting" in terminology
  assert "transition_cost(current, neighbour)" in tutorial
  assert "entry_cost(neighbour)" in tutorial
  assert "current cell" not in tutorial
  assert "current cell" not in model


def test_browser_surface_is_accessible_responsive_and_motion_aware():
  """DOM controls and the browser harness cover behavior beyond the canvas."""
  html = read("examples/web_flow_steering/site/index.html")
  app = read("examples/web_flow_steering/site/app.js")
  interactions = read("tools/test_web_demo_interactions.py")

  assert "<title>Flow field steering · tess</title>" in html
  assert "<h1>Flow field steering</h1>" in html
  assert 'id="pause"' in html
  assert 'id="reset"' in html
  assert 'id="goal-x"' in html
  assert 'id="goal-y"' in html
  assert 'id="set-goal"' in html
  assert 'data-goal-preset=' in html
  assert html.count(" disabled") >= 8
  assert 'id="announcement"' in html
  assert html.count('aria-live="polite"') == 1
  assert 'class="status" aria-live=' not in html
  assert "aria-pressed" not in html
  assert 'aria-label="Flow field steering grid"' in html
  assert "Independent agents may overlap" in html
  assert "At goal" in html
  assert "Unreachable" in html
  assert "matchMedia(\"(prefers-reduced-motion: reduce)\")" in app
  assert "canvas.getBoundingClientRect().width" in app
  assert "dataset.distanceLabels" in app
  assert "Number.isInteger" in app
  assert "checkValidity()" in app
  assert "is outside the world" in app
  assert "is impassable" in app
  assert "function reportGoalRejection" in app
  assert 'canvas.addEventListener("click"' in app
  assert 'canvas.addEventListener("pointerdown"' not in app
  assert "aria-pressed" not in app
  assert "control.disabled = false" in app
  assert 'canvas.removeAttribute("aria-disabled")' in app
  assert "data.tessFlowSteering = \"ready\"" in app
  assert "requestAnimationFrame" in app
  assert "ResizeObserver" not in app
  assert "postMessage" not in app

  assert "def test_flow_steering" in interactions
  assert 'flow-steering/?browser-test=1' in interactions
  assert "prefers-reduced-motion" in interactions
  assert "data-goal-preset" in interactions
  assert "#set-goal" in interactions
  assert "distanceLabels" in interactions
  assert "whole-number coordinates" in interactions
  assert "announcement" in interactions
  assert "dispatchEvent(new Event('resize'))" in interactions
  assert "noOverflow" in interactions
  assert "frameOverflow" in interactions


def test_ci_schedules_static_native_wasm_and_browser_checks():
  """Every verification layer is reachable from required workflows."""
  ci = read(".github/workflows/ci.yml")
  pages = read(".github/workflows/pages.yml")

  assert "tests/test_flow_steering_tutorial.py" in ci
  assert "bash tools/build_web_demo.sh build/site/demo" in pages
  assert "python3 tools/test_web_demo_interactions.py" in pages
  assert "if [[ -d examples/web_flow_steering ]]" in pages
  assert "test -d docs/demo/flow-steering" in pages
