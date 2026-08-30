#include <GLFW/glfw3.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <emscripten/emscripten.h>
#include <imgui.h>
#include <tess/debug/imgui/panels.h>

#include <algorithm>
#include <cstdint>
#include <limits>

#include "diagnostics_model.h"

namespace demo = tess::examples::web_diagnostics;

namespace {

struct BrowserApp {
  GLFWwindow* window = nullptr;
  demo::DiagnosticsModel model;
  int status = 0;
};

struct PanelLayout {
  ImVec2 diagnostics_position;
  ImVec2 diagnostics_size;
  ImVec2 controls_position;
  ImVec2 controls_size;
};

auto app() -> BrowserApp& {
  static BrowserApp value;
  return value;
}

[[nodiscard]] auto to_int(std::uint64_t value) noexcept -> int {
  return static_cast<int>(std::min(
      value, static_cast<std::uint64_t>(std::numeric_limits<int>::max())));
}

[[nodiscard]] auto panel_layout() -> PanelLayout {
  const auto display = ImGui::GetIO().DisplaySize;
  constexpr auto margin = 10.0F;
  constexpr auto gap = 10.0F;
  if (display.x >= 760.0F) {
    const auto diagnostics_width = display.x * 0.61F;
    const auto controls_width =
        display.x - diagnostics_width - 2.0F * margin - gap;
    const auto height = display.y - 2.0F * margin;
    return {{margin, margin},
            {diagnostics_width, height},
            {margin + diagnostics_width + gap, margin},
            {controls_width, height}};
  }
  const auto width = display.x - 2.0F * margin;
  const auto diagnostics_height = display.y * 0.58F;
  const auto controls_height =
      display.y - diagnostics_height - 2.0F * margin - gap;
  return {{margin, margin},
          {width, diagnostics_height},
          {margin, margin + diagnostics_height + gap},
          {width, controls_height}};
}

// Example-local presentation over the UI-agnostic snapshot. Keeping this out
// of tess avoids adding a public panel API during the release-candidate cycle.
void draw_flow_health_panel(const tess::diagnostics::FlowHealthSnapshot& flow) {
  const auto& counters = flow.counters;
  ImGui::TextUnformatted("Lifecycle flow accounting");
  ImGui::Separator();
  ImGui::Text("admission identity: %s",
              flow.admission_identity_ok ? "holds" : "BROKEN");
  ImGui::Text("retention identity: %s",
              flow.retention_identity_ok ? "holds" : "BROKEN");
  ImGui::Text("offered / admitted / rejected: %llu / %llu / %llu",
              static_cast<unsigned long long>(counters.offered),
              static_cast<unsigned long long>(counters.admitted),
              static_cast<unsigned long long>(counters.rejected));
  ImGui::Text("outstanding / high-water: %llu / %llu",
              static_cast<unsigned long long>(counters.outstanding_current),
              static_cast<unsigned long long>(counters.outstanding_high_water));
  ImGui::Text("completed / cancelled / superseded: %llu / %llu / %llu",
              static_cast<unsigned long long>(counters.completed),
              static_cast<unsigned long long>(counters.cancelled),
              static_cast<unsigned long long>(counters.superseded));
  ImGui::Text(
      "stale / failed / dropped: %llu / %llu / %llu",
      static_cast<unsigned long long>(counters.stale),
      static_cast<unsigned long long>(counters.failed),
      static_cast<unsigned long long>(counters.dropped_after_admission));
  ImGui::Text("planning work offered / consumed: %llu / %llu",
              static_cast<unsigned long long>(counters.offered_work_units),
              static_cast<unsigned long long>(counters.consumed_work_units));
}

void draw_controls(BrowserApp& value, const PanelLayout& layout) {
  ImGui::SetNextWindowPos(layout.controls_position, ImGuiCond_Always);
  ImGui::SetNextWindowSize(layout.controls_size, ImGuiCond_Always);
  ImGui::Begin("Colony workload and world");
  auto paused = value.model.paused();
  if (ImGui::Checkbox("Paused", &paused)) {
    value.model.set_paused(paused);
  }
  if (ImGui::Button("Reset colony")) {
    value.model.reset();
  }
  auto x = value.model.selected_x();
  auto y = value.model.selected_y();
  auto changed = ImGui::SliderInt("Selected x", &x, 18, 109);
  changed = ImGui::SliderInt("Selected y", &y, 0, 127) || changed;
  if (changed) {
    (void)value.model.select(x, y);
  }
  auto passable = value.model.selected_passable();
  if (ImGui::Checkbox("Passable", &passable)) {
    (void)value.model.set_passable(x, y, passable);
  }
  ImGui::Separator();
  ImGui::Text("fixed ticks: %llu",
              static_cast<unsigned long long>(value.model.fixed_ticks()));
  ImGui::Text("planning queries / expansions: %d / %d",
              value.model.planning_queries(),
              value.model.planning_expansions());
  ImGui::Text(
      "queued calls / dirty merged: %llu / %llu",
      static_cast<unsigned long long>(value.model.queued_phase_calls()),
      static_cast<unsigned long long>(value.model.queued_dirty_merged()));
  ImGui::TextWrapped(
      "Lifecycle flow accounting tracks admitted goals and terminal "
      "outcomes. It is not a pathfinding flow field.");
  ImGui::End();
}

void draw_frame(void*) {
  auto& value = app();
  glfwPollEvents();
  if (glfwWindowShouldClose(value.window) != 0) {
    value.status = -1;
    emscripten_cancel_main_loop();
    return;
  }
  if (!value.model.tick()) {
    value.status = -1;
  }

  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();
  const auto layout = panel_layout();
  draw_controls(value, layout);
  ImGui::SetNextWindowPos(layout.diagnostics_position, ImGuiCond_Always);
  ImGui::SetNextWindowSize(layout.diagnostics_size, ImGuiCond_Always);
  ImGui::Begin("tess diagnostics (consumer-instrumented allocations)");
  draw_flow_health_panel(value.model.flow_snapshot());
  ImGui::Separator();
  tess::debug::imgui::draw_diagnostics_panel(value.model.snapshot());
  ImGui::End();
  ImGui::Render();

  int display_width = 0;
  int display_height = 0;
  glfwGetFramebufferSize(value.window, &display_width, &display_height);
  glViewport(0, 0, display_width, display_height);
  glClearColor(0.055F, 0.043F, 0.082F, 1.0F);
  glClear(GL_COLOR_BUFFER_BIT);
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

  const auto rendered = ImGui::GetDrawData() != nullptr &&
                        ImGui::GetDrawData()->TotalVtxCount > 0;
  value.model.note_imgui_frame(rendered);
  if (value.status >= 0 && value.model.ready()) {
    value.status = 1;
  }
}

[[nodiscard]] auto initialize() -> bool {
  auto& value = app();
  if (glfwInit() == GLFW_FALSE) {
    return false;
  }
  glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
  value.window =
      glfwCreateWindow(1280, 800, "tess diagnostics", nullptr, nullptr);
  if (value.window == nullptr) {
    return false;
  }
  glfwMakeContextCurrent(value.window);
  glfwSwapInterval(1);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::GetIO().IniFilename = nullptr;
  ImGui::StyleColorsDark();
  if (!ImGui_ImplGlfw_InitForOpenGL(value.window, true) ||
      !ImGui_ImplOpenGL3_Init("#version 300 es")) {
    return false;
  }
  ImGui_ImplGlfw_InstallEmscriptenCallbacks(value.window, "#canvas");
  value.model.note_runtime_initialized();
  return value.model.tick();
}

}  // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE auto tess_diagnostics_status() -> int {
  return app().status;
}

EMSCRIPTEN_KEEPALIVE void tess_diagnostics_set_paused(int value) {
  app().model.set_paused(value != 0);
}

EMSCRIPTEN_KEEPALIVE void tess_diagnostics_reset() { app().model.reset(); }

EMSCRIPTEN_KEEPALIVE auto tess_diagnostics_select(int x, int y) -> int {
  return app().model.select(x, y) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE auto tess_diagnostics_set_passable(int x, int y, int value)
    -> int {
  return app().model.set_passable(x, y, value != 0) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE auto tess_diagnostics_paused() -> int {
  return app().model.paused() ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE auto tess_diagnostics_selected_x() -> int {
  return app().model.selected_x();
}

EMSCRIPTEN_KEEPALIVE auto tess_diagnostics_selected_y() -> int {
  return app().model.selected_y();
}

EMSCRIPTEN_KEEPALIVE auto tess_diagnostics_selected_passable() -> int {
  return app().model.selected_passable() ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE auto tess_diagnostics_fixed_ticks() -> int {
  return to_int(app().model.fixed_ticks());
}

EMSCRIPTEN_KEEPALIVE auto tess_diagnostics_path_passability_checks() -> int {
  return to_int(app().model.path_passability_checks());
}

EMSCRIPTEN_KEEPALIVE auto tess_diagnostics_queued_phase_calls() -> int {
  return to_int(app().model.queued_phase_calls());
}

EMSCRIPTEN_KEEPALIVE auto tess_diagnostics_queued_dirty_merged() -> int {
  return to_int(app().model.queued_dirty_merged());
}

EMSCRIPTEN_KEEPALIVE auto tess_diagnostics_planning_queries() -> int {
  return app().model.planning_queries();
}

EMSCRIPTEN_KEEPALIVE auto tess_diagnostics_planning_expansions() -> int {
  return app().model.planning_expansions();
}

EMSCRIPTEN_KEEPALIVE auto tess_diagnostics_flow_offered() -> int {
  return to_int(app().model.flow_snapshot().counters.offered);
}

EMSCRIPTEN_KEEPALIVE auto tess_diagnostics_flow_admitted() -> int {
  return to_int(app().model.flow_snapshot().counters.admitted);
}

EMSCRIPTEN_KEEPALIVE auto tess_diagnostics_flow_terminal() -> int {
  return to_int(app().model.flow_snapshot().counters.terminal());
}

EMSCRIPTEN_KEEPALIVE auto tess_diagnostics_flow_outstanding() -> int {
  return to_int(app().model.flow_snapshot().counters.outstanding_current);
}

EMSCRIPTEN_KEEPALIVE auto tess_diagnostics_flow_high_water() -> int {
  return to_int(app().model.flow_snapshot().counters.outstanding_high_water);
}

EMSCRIPTEN_KEEPALIVE auto tess_diagnostics_flow_admission_ok() -> int {
  return app().model.flow_snapshot().admission_identity_ok ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE auto tess_diagnostics_flow_retention_ok() -> int {
  return app().model.flow_snapshot().retention_identity_ok ? 1 : 0;
}

}  // extern "C"

auto main() -> int {
  if (!initialize()) {
    app().status = -1;
    return 1;
  }
  emscripten_set_main_loop_arg(draw_frame, nullptr, 0, true);
  return 0;
}
