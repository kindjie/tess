#include "traffic_model.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define TESS_TRAFFIC_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define TESS_TRAFFIC_EXPORT
#endif

#include <cstdint>
#include <memory>

namespace demo = tess::examples::web_traffic;

namespace {

std::unique_ptr<demo::TrafficModel> model;

auto parse_scenario(int value) -> demo::TrafficScenario {
  if (value < static_cast<int>(demo::TrafficScenario::Aligned) ||
      value > static_cast<int>(demo::TrafficScenario::MultiGate)) {
    return demo::TrafficScenario::Aligned;
  }
  return static_cast<demo::TrafficScenario>(value);
}

}  // namespace

extern "C" {

TESS_TRAFFIC_EXPORT int tess_traffic_width() { return demo::traffic_width; }
TESS_TRAFFIC_EXPORT int tess_traffic_height() { return demo::traffic_height; }
TESS_TRAFFIC_EXPORT int tess_traffic_agent_count() {
  return demo::traffic_agents;
}
TESS_TRAFFIC_EXPORT int tess_traffic_reset(int scenario) {
  const auto selected = parse_scenario(scenario);
  if (model) {
    model->reset(selected);
  } else {
    model = std::make_unique<demo::TrafficModel>(selected);
  }
  return static_cast<int>(model->scenario());
}
TESS_TRAFFIC_EXPORT double tess_traffic_tick(double dt_seconds) {
  return model ? model->tick(dt_seconds) : -1.0;
}
TESS_TRAFFIC_EXPORT const std::uint8_t* tess_traffic_terrain() {
  return model ? model->terrain() : nullptr;
}
TESS_TRAFFIC_EXPORT const std::int16_t* tess_traffic_agents() {
  return model ? model->current_agents() : nullptr;
}
TESS_TRAFFIC_EXPORT const std::int16_t* tess_traffic_previous_agents() {
  return model ? model->previous_agents() : nullptr;
}
TESS_TRAFFIC_EXPORT double tess_traffic_interpolation_alpha() {
  return model ? model->interpolation_alpha() : 0.0;
}
TESS_TRAFFIC_EXPORT double tess_traffic_planning_us() {
  return model ? model->planning_us() : 0.0;
}
TESS_TRAFFIC_EXPORT int tess_traffic_planning_queries() {
  return model ? model->planning_queries_last_tick() : 0;
}
TESS_TRAFFIC_EXPORT int tess_traffic_fixed_ticks() {
  return model ? model->fixed_ticks_last_call() : 0;
}
TESS_TRAFFIC_EXPORT int tess_traffic_planning_pending() {
  return model ? model->planning_pending() : 0;
}
TESS_TRAFFIC_EXPORT int tess_traffic_advanced() {
  return model ? model->advanced_last_tick() : 0;
}
TESS_TRAFFIC_EXPORT int tess_traffic_waits() {
  return model ? model->movement_waits_last_tick() : 0;
}
TESS_TRAFFIC_EXPORT int tess_traffic_blocked() {
  return model ? model->blocked_agents() : 0;
}
TESS_TRAFFIC_EXPORT int tess_traffic_arrived() {
  return model ? model->arrived_agents() : 0;
}
TESS_TRAFFIC_EXPORT int tess_traffic_one_progress_streak() {
  return model ? model->one_progress_streak() : 0;
}
TESS_TRAFFIC_EXPORT int tess_traffic_longest_one_progress_streak() {
  return model ? model->longest_one_progress_streak() : 0;
}
}  // extern "C"

#ifdef __EMSCRIPTEN__
int main() { return 0; }
#endif
