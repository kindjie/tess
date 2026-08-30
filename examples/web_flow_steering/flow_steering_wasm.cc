#include "flow_steering_model.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define TESS_FLOW_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define TESS_FLOW_EXPORT
#endif

#include <cstdint>
#include <memory>

namespace flow = tess::examples::web_flow_steering;

namespace {

std::unique_ptr<flow::FlowSteeringModel> model;

[[nodiscard]] auto state() -> flow::FlowSteeringModel& {
  if (!model) {
    model = std::make_unique<flow::FlowSteeringModel>();
  }
  return *model;
}

}  // namespace

extern "C" {

TESS_FLOW_EXPORT int tess_flow_width() { return flow::width; }
TESS_FLOW_EXPORT int tess_flow_height() { return flow::height; }

TESS_FLOW_EXPORT void tess_flow_reset() {
  model = std::make_unique<flow::FlowSteeringModel>();
}

TESS_FLOW_EXPORT int tess_flow_tick() { return state().tick(); }

TESS_FLOW_EXPORT int tess_flow_set_goal(int x, int y) {
  return state().set_goal(x, y) ? 1 : 0;
}

TESS_FLOW_EXPORT int tess_flow_goal_x() { return state().goal_x(); }
TESS_FLOW_EXPORT int tess_flow_goal_y() { return state().goal_y(); }
TESS_FLOW_EXPORT int tess_flow_agent_count() { return state().agent_count(); }
TESS_FLOW_EXPORT int tess_flow_agent_x(int index) {
  return state().agent_x(index);
}
TESS_FLOW_EXPORT int tess_flow_agent_y(int index) {
  return state().agent_y(index);
}
TESS_FLOW_EXPORT int tess_flow_agent_state(int index) {
  return static_cast<int>(state().agent_state(index));
}
TESS_FLOW_EXPORT int tess_flow_tile_passable(int x, int y) {
  return state().tile_passable(x, y) ? 1 : 0;
}
TESS_FLOW_EXPORT std::uint32_t tess_flow_tile_distance(int x, int y) {
  return state().tile_distance(x, y);
}

}  // extern "C"
