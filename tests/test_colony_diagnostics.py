"""Static contracts for the colony-backed diagnostics tutorial host."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
  return (ROOT / path).read_text()


def test_diagnostics_target_instruments_every_shared_model_translation_unit():
  """The host cannot observe code compiled without the diagnostics gate."""
  cmake = read("examples/CMakeLists.txt")
  target = cmake.split("tess_web_diagnostics_model", 1)[1].split(")", 1)[0]
  assert "web_colony/colony_model.cc" in target
  assert "web_diagnostics/diagnostics_model.cc" in target
  assert "web_diagnostics/diagnostics_native.cc" in target

  build = read("tools/build_web_demo.sh")
  diagnostics = build.split("# Reference diagnostics integration:", 1)[1]
  assert "examples/web_colony/colony_model.cc" in diagnostics
  assert "-DTESS_ENABLE_DIAGNOSTICS" in diagnostics


def test_flow_accounting_is_attached_before_colony_goals_are_admitted():
  """Constructor injection is the example-local lifecycle authority seam."""
  colony_header = read("examples/web_colony/colony_model.h")
  colony_model = read("examples/web_colony/colony_model.cc")
  diagnostics = read("examples/web_diagnostics/diagnostics_model.cc")

  assert "diagnostics::FlowAccounting* flow_accounting" in colony_header
  constructor = colony_model.split("ColonyModel::Impl::Impl", 1)[1]
  assert (
    constructor.index("tick_state.flow_accounting = flow_accounting")
    < constructor.index("initialize_agents(count)")
  )
  assert "make_unique<web_colony::ColonyModel>" in diagnostics
  assert "&flow_accounting_" in diagnostics
  assert "diagnostics::snapshot(flow_accounting_)" in diagnostics


def test_host_replaces_the_synthetic_workload_without_expanding_tess_api():
  header = read("examples/web_diagnostics/diagnostics_model.h")
  source = read("examples/web_diagnostics/diagnostics_model.cc")
  wasm = read("examples/web_diagnostics/diagnostics_wasm.cc")
  panels = read("include/tess/debug/imgui/panels.h")

  assert "web_colony/colony_model.h" in header
  assert "struct PassableTag" not in header
  assert "AlwaysResidentWorld" not in header
  assert "run_path_workload" not in source
  assert "run_queued_workload" not in source
  assert "draw_flow_health_panel" in wasm
  assert "draw_flow_health_panel" not in panels


def test_accessible_surface_and_docs_name_lifecycle_flow_accounting():
  html = read("examples/web_diagnostics/site/index.html")
  app = read("examples/web_diagnostics/site/app.js")
  guide = read("docs/guide/diagnostics.md")
  architecture = read("docs/architecture/diagnostics.md")
  terminology = read("docs/terminology.md")
  abbreviations = read("includes/abbreviations.md")

  for control in ('id="paused"', 'id="reset"', 'id="selected-x"',
                  'id="selected-y"', 'id="passable"'):
    assert control in html
  for state in ("flow-admission", "flow-retention", "flow-outstanding",
                "planning-work", "queued-work"):
    assert f'id="{state}"' in html
    assert f'querySelector("#{state}")' in app
  assert "lifecycle flow accounting" in guide.lower()
  assert "pathfinding flow field" in guide.lower()
  assert "reserved warm ticks" in architecture.lower()
  assert "lifecycle flow accounting" in terminology.lower()
  assert "lifecycle flow accounting" in abbreviations.lower()
