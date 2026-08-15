#include <GLFW/glfw3.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <emscripten/emscripten.h>
#include <imgui.h>
#include <tess/debug/imgui/panels.h>
#include <tess/debug/imgui/tools.h>

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

void draw_controls(BrowserApp& value, const PanelLayout& layout) {
  ImGui::SetNextWindowPos(layout.controls_position, ImGuiCond_Always);
  ImGui::SetNextWindowSize(layout.controls_size, ImGuiCond_Always);
  ImGui::Begin("Workload and world");
  auto paused = value.model.paused();
  if (ImGui::Checkbox("Paused", &paused)) {
    value.model.set_paused(paused);
  }
  auto intensity = value.model.intensity();
  if (ImGui::SliderInt("Searches/frame", &intensity, 1, 32)) {
    value.model.set_intensity(intensity);
  }
  auto selected = value.model.selected();
  auto x = static_cast<int>(selected.x);
  auto y = static_cast<int>(selected.y);
  auto changed = ImGui::SliderInt("Selected x", &x, 0, demo::width - 1);
  changed = ImGui::SliderInt("Selected y", &y, 0, demo::height - 1) || changed;
  if (changed) {
    (void)value.model.select(x, y);
    selected = value.model.selected();
  }
  ImGui::Separator();
  tess::debug::imgui::draw_world_overview(value.model.world());
  ImGui::Separator();
  (void)tess::debug::imgui::draw_chunk_inspector(value.model.world(), selected);
  const auto edit =
      tess::debug::imgui::draw_bool_field_editor<demo::PassableTag>(
          value.model.world(), selected, "Passable");
  if (edit.intent.has_value()) {
    const auto& intent = *edit.intent;
    (void)value.model.set_passable(static_cast<int>(intent.tile.x),
                                   static_cast<int>(intent.tile.y),
                                   intent.value);
  }
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

EMSCRIPTEN_KEEPALIVE void tess_diagnostics_set_intensity(int value) {
  app().model.set_intensity(value);
}

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

EMSCRIPTEN_KEEPALIVE auto tess_diagnostics_intensity() -> int {
  return app().model.intensity();
}

EMSCRIPTEN_KEEPALIVE auto tess_diagnostics_selected_x() -> int {
  return static_cast<int>(app().model.selected().x);
}

EMSCRIPTEN_KEEPALIVE auto tess_diagnostics_selected_y() -> int {
  return static_cast<int>(app().model.selected().y);
}

EMSCRIPTEN_KEEPALIVE auto tess_diagnostics_selected_passable() -> int {
  return app().model.selected_passable() ? 1 : 0;
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
