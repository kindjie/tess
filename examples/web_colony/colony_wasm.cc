#include "colony_model.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define TESS_COLONY_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define TESS_COLONY_EXPORT
#endif

#include <cstdint>
#include <memory>

namespace demo = tess::examples::web_colony;

namespace {

std::unique_ptr<demo::ColonyModel> model;

}  // namespace

extern "C" {

TESS_COLONY_EXPORT int tess_colony_width() { return demo::width; }
TESS_COLONY_EXPORT int tess_colony_height() { return demo::height; }

TESS_COLONY_EXPORT int tess_colony_reset(int agent_count) {
  model = std::make_unique<demo::ColonyModel>(agent_count);
  return model->agent_count();
}

TESS_COLONY_EXPORT int tess_colony_set_wall(int x, int y, int built) {
  return model && model->set_wall(x, y, built != 0) ? 1 : 0;
}

TESS_COLONY_EXPORT void tess_colony_set_strategy(int replan_each_tick) {
  if (model) {
    model->set_replan_each_tick(replan_each_tick != 0);
  }
}

TESS_COLONY_EXPORT void tess_colony_set_spread(int enabled) {
  if (model) {
    model->set_spread_congested_routes(enabled != 0);
  }
}

TESS_COLONY_EXPORT void tess_colony_set_pricing(int policy) {
  if (model) {
    model->set_congestion_pricing(policy);
  }
}

TESS_COLONY_EXPORT double tess_colony_tick(double dt_seconds) {
  return model ? model->tick(dt_seconds) : -1.0;
}

TESS_COLONY_EXPORT int tess_colony_relaunch() {
  return model ? model->relaunch() : 0;
}

TESS_COLONY_EXPORT int tess_colony_leg() { return model ? model->leg() : 0; }

TESS_COLONY_EXPORT int tess_colony_completed_legs() {
  return model ? model->completed_legs() : 0;
}

TESS_COLONY_EXPORT int tess_colony_aborted_legs() {
  return model ? model->aborted_legs() : 0;
}

TESS_COLONY_EXPORT const std::uint8_t* tess_colony_tiles() {
  return model ? model->tiles() : nullptr;
}

TESS_COLONY_EXPORT const std::int16_t* tess_colony_agents() {
  return model ? model->current_agents() : nullptr;
}

TESS_COLONY_EXPORT const std::int16_t* tess_colony_previous_agents() {
  return model ? model->previous_agents() : nullptr;
}

TESS_COLONY_EXPORT double tess_colony_interpolation_alpha() {
  return model ? model->interpolation_alpha() : 0.0;
}

TESS_COLONY_EXPORT int tess_colony_agent_count() {
  return model ? model->agent_count() : 0;
}

TESS_COLONY_EXPORT int tess_colony_arrived() {
  return model ? model->arrived() : 0;
}

TESS_COLONY_EXPORT int tess_colony_unreachable() {
  return model ? model->unreachable() : 0;
}

TESS_COLONY_EXPORT int tess_colony_crowd_blocked() {
  return model ? model->crowd_blocked() : 0;
}

TESS_COLONY_EXPORT int tess_colony_turnaround_ready() {
  return model && model->turnaround_ready() ? 1 : 0;
}

TESS_COLONY_EXPORT int tess_colony_stalled_ticks() {
  return model ? model->stalled_ticks() : 0;
}

TESS_COLONY_EXPORT int tess_colony_planning_pending() {
  return model ? model->planning_pending() : 0;
}

TESS_COLONY_EXPORT int tess_colony_advanced_last_tick() {
  return model ? model->advanced_last_tick() : 0;
}

TESS_COLONY_EXPORT int tess_colony_movement_waits_last_tick() {
  return model ? model->movement_waits_last_tick() : 0;
}

}  // extern "C"

#ifdef __EMSCRIPTEN__
int main() { return 0; }
#endif
