"""Contracts for the procedural sparse-stream tutorial and live model."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
  return (ROOT / relative).read_text(encoding="utf-8")


def test_native_model_owns_the_bounded_streaming_protocol():
  header = read("examples/web_sparse_stream/sparse_stream_model.h")
  model = read("examples/web_sparse_stream/sparse_stream_model.cc")
  native = read("examples/web_sparse_stream/sparse_stream_native.cc")
  cmake = read("examples/CMakeLists.txt")

  assert "world_width = 4096" in header
  assert "world_height = 4096" in header
  assert "chunk_size = 32" in header
  assert "resident_capacity = 32" in header
  assert "camera_window_width = 5" in header
  assert (
    "Restores the initial camera, agents, and goals, then materializes\n"
    "  /// the initial resident window."
  ) in header
  assert "tess::SparseResidentWorld" in model
  assert "tess::MissingChunkPolicy::ReportIndeterminate" in model
  assert "required.size() > impl_->world.capacity()" in model
  assert ".evict(" not in model

  residency = model.split("// [sparse-stream-residency-order]", 2)[1]
  assert residency.index("previous_resident") < residency.index("touch(key)")
  assert residency.index("touch(key)") < residency.index("ensure_resident")
  assert residency.index("ensure_resident") < residency.index("generate_chunk")
  assert residency.index("generate_chunk") < residency.index("refresh_view")
  assert residency.index("refresh_view") < residency.index("astar_path")
  assert (
    "if (materialized) {\n"
    "    for (const auto key : impl_->newly_generated)" in residency
  )

  for invariant in (
    "check_capacity_preflight",
    "check_regeneration_is_byte_identical",
    "check_indeterminate_retry",
    "check_generates_only_newly_materialized_pages",
    "check_determinism_and_legal_steps",
    "check_bounded_camera_follow_stream",
  ):
    assert invariant in native
  bounded = native.split("check_bounded_camera_follow_stream", 1)[1]
  assert "tick < 2200" in bounded
  assert "procedural sparse stream model: ok" in native
  assert "tess_web_sparse_stream_model" in cmake
  assert "tess_web_sparse_stream_wasm_adapter" in cmake
  assert "tess_web_sparse_stream_self_check" in cmake


def test_wasm_adapter_is_a_narrow_view_over_the_shared_model():
  wasm = read("examples/web_sparse_stream/sparse_stream_wasm.cc")
  build = read("tools/build_web_demo.sh")

  for name in (
    "reset",
    "tick",
    "status",
    "camera_chunk_x",
    "camera_chunk_y",
    "resident_count",
    "required_count",
    "new_count",
    "retained_count",
    "evicted_count",
    "required_chunk_x",
    "required_chunk_y",
    "new_chunk_x",
    "new_chunk_y",
    "retained_chunk_x",
    "retained_chunk_y",
    "evicted_chunk_x",
    "evicted_chunk_y",
    "agent_count",
    "agent_x",
    "agent_y",
    "agent_goal_x",
    "agent_goal_y",
    "agent_status",
    "step_count",
  ):
    assert f"tess_sparse_{name}" in wasm
    assert f'_tess_sparse_{name}' in build
  assert "ensure_resident" not in wasm
  assert "astar_path" not in wasm
  assert 'sparse_stream="$output/sparse-stream"' in build
  assert "createTessSparseStream" in build


def test_tutorial_teaches_the_supported_boundary_and_production_extensions():
  tutorial = read("docs/tutorial/procedural-sparse-stream.md")
  model = read("examples/web_sparse_stream/sparse_stream_model.cc")
  styles = read("docs/stylesheets/extra.css")
  nav = read("mkdocs.yml")
  tutorials = read("docs/tutorials.md")
  examples = read("docs/examples.md")

  for phrase in (
    "large and bounded",
    "4,096×4,096",
    "32×32 chunks",
    "32-page",
    "5×5 camera window",
    "inner 3×3",
    "fail before mutation",
    "least-recently-used",
    "ReportIndeterminate",
    "asynchronous generation",
    "persistence of edits",
    "predictive prefetching",
    "separate rendering and simulation radii",
    "pinning",
    "back-pressure",
    "long paths crossing unavailable regions",
  ):
    assert phrase in tutorial
  assert "infinite" in tutorial
  assert "not infinite" in tutorial
  assert "persistent edits" in tutorial
  assert "collision avoidance" in tutorial
  assert '!!! info "API used"' in tutorial
  assert "https://tess.owx.dev/api/classtess_1_1World.html" in tutorial
  assert (
    "https://tess.owx.dev/api/classtess_1_1PathScratch.html"
    "#a7b7d735ab95ab0db2275b679188873b4"
  ) in tutorial
  assert 'class="sparse-stream-frame"' in tutorial
  assert 'src="../../demo/sparse-stream/"' in tutorial
  assert 'title="Interactive procedural sparse-stream tutorial"' in tutorial
  assert 'aria-label="Interactive procedural sparse-stream tutorial"' in tutorial
  assert "Open the sparse-stream example in a separate page" in tutorial
  assert "sparse-stream-residency-order" in model
  assert ".sparse-stream-frame" in styles
  assert "Procedural sparse stream: tutorial/procedural-sparse-stream.md" in nav
  assert "procedural sparse" in tutorials.lower()
  assert "web_sparse_stream" in examples


def test_browser_surface_is_accessible_responsive_and_motion_aware():
  html = read("examples/web_sparse_stream/site/index.html")
  app = read("examples/web_sparse_stream/site/app.js")
  styles = read("examples/web_sparse_stream/site/style.css")
  interactions = read("tools/test_web_demo_interactions.py")

  assert 'id="pause"' in html
  assert 'id="reset"' in html
  assert 'id="stream-world"' in html
  assert 'id="summary"' in html
  assert 'id="residency-status"' in html
  assert 'id="agent-status"' in html
  assert 'id="announcement"' in html
  assert 'aria-live="polite"' in html
  for label in ("Required", "New", "Retained", "Evicted"):
    assert label in html
  assert "matchMedia(\"(prefers-reduced-motion: reduce)\")" in app
  assert "requestAnimationFrame" in app
  assert "ResizeObserver" not in app
  assert "postMessage" not in app
  assert 'dataset.tessSparseStream = "ready"' in app
  assert "window.tessSparseStreamTest" in app
  assert "def test_sparse_stream" in interactions
  assert "tessSparseStreamTest.step(72)" in interactions
  assert "prefers-reduced-motion" in interactions
  assert "noOverflow" in interactions
  assert "frameOverflow" in interactions
  assert "overflow-x: hidden" in styles
  assert "overflow-y: auto" in styles
  assert "with open_page(browser, url, 844, 390, timeout)" in interactions
  assert "scrollHeight > innerHeight" in interactions


def test_ci_schedules_static_native_wasm_and_browser_checks():
  ci = read(".github/workflows/ci.yml")
  pages = read(".github/workflows/pages.yml")
  finalizer = read("tools/finalize_generated_pages.py")

  assert "tests/test_sparse_stream_tutorial.py" in ci
  assert "bash tools/build_web_demo.sh build/site/demo" in pages
  assert "python3 tools/test_web_demo_interactions.py" in pages
  assert "if [[ -d examples/web_sparse_stream ]]" in pages
  sparse_guard = pages.split(
    "if [[ -d examples/web_sparse_stream ]]", 1
  )[1].split("fi", 1)[0]
  assert "test -d docs/demo/sparse-stream" in sparse_guard
  assert '"sparse-stream": (' in finalizer
