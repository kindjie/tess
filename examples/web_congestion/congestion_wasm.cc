#include "../web_colony/colony_model.h"
#include "congestion_model.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define TESS_CONGESTION_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define TESS_CONGESTION_EXPORT
#endif

#include <cstdint>
#include <memory>

namespace demo = tess::examples::web_congestion;

namespace {

std::unique_ptr<demo::CongestionModel> model;

}  // namespace

extern "C" {

TESS_CONGESTION_EXPORT int tess_congestion_width() {
  return tess::examples::web_colony::width;
}
TESS_CONGESTION_EXPORT int tess_congestion_height() {
  return tess::examples::web_colony::height;
}

TESS_CONGESTION_EXPORT int tess_congestion_reset(int agent_count) {
  model = std::make_unique<demo::CongestionModel>(agent_count);
  return model->agent_count();
}

TESS_CONGESTION_EXPORT int tess_congestion_set_wall(int x, int y, int built) {
  return model && model->set_wall(x, y, built != 0) ? 1 : 0;
}

TESS_CONGESTION_EXPORT void tess_congestion_set_strategy(int replan_each_tick) {
  if (model) {
    model->set_replan_each_tick(replan_each_tick != 0);
  }
}

TESS_CONGESTION_EXPORT void tess_congestion_set_spread(int enabled) {
  if (model) {
    model->set_spread_congested_routes(enabled != 0);
  }
}

TESS_CONGESTION_EXPORT void tess_congestion_set_pricing(int policy) {
  if (model) {
    model->set_pricing_policy(policy);
  }
}

TESS_CONGESTION_EXPORT const std::uint8_t* tess_congestion_prices() {
  return model ? model->prices() : nullptr;
}

TESS_CONGESTION_EXPORT double tess_congestion_scoped_replans() {
  return model ? static_cast<double>(model->scoped_replans()) : 0.0;
}

TESS_CONGESTION_EXPORT double tess_congestion_tick(double dt_seconds) {
  return model ? model->tick(dt_seconds) : -1.0;
}

TESS_CONGESTION_EXPORT int tess_congestion_relaunch() {
  return model ? model->relaunch() : 0;
}

TESS_CONGESTION_EXPORT int tess_congestion_leg() {
  return model ? model->leg() : 0;
}

TESS_CONGESTION_EXPORT int tess_congestion_completed_legs() {
  return model ? model->completed_legs() : 0;
}

TESS_CONGESTION_EXPORT int tess_congestion_aborted_legs() {
  return model ? model->aborted_legs() : 0;
}

TESS_CONGESTION_EXPORT const std::uint8_t* tess_congestion_tiles() {
  return model ? model->tiles() : nullptr;
}

TESS_CONGESTION_EXPORT const std::int16_t* tess_congestion_agents() {
  return model ? model->current_agents() : nullptr;
}

TESS_CONGESTION_EXPORT const std::int16_t* tess_congestion_previous_agents() {
  return model ? model->previous_agents() : nullptr;
}

TESS_CONGESTION_EXPORT double tess_congestion_interpolation_alpha() {
  return model ? model->interpolation_alpha() : 0.0;
}

TESS_CONGESTION_EXPORT int tess_congestion_agent_count() {
  return model ? model->agent_count() : 0;
}

TESS_CONGESTION_EXPORT int tess_congestion_arrived() {
  return model ? model->arrived() : 0;
}

TESS_CONGESTION_EXPORT int tess_congestion_unreachable() {
  return model ? model->unreachable() : 0;
}

TESS_CONGESTION_EXPORT int tess_congestion_crowd_blocked() {
  return model ? model->crowd_blocked() : 0;
}

TESS_CONGESTION_EXPORT int tess_congestion_turnaround_ready() {
  return model && model->turnaround_ready() ? 1 : 0;
}

TESS_CONGESTION_EXPORT int tess_congestion_stalled_ticks() {
  return model ? model->stalled_ticks() : 0;
}

TESS_CONGESTION_EXPORT int tess_congestion_planning_pending() {
  return model ? model->planning_pending() : 0;
}

TESS_CONGESTION_EXPORT int tess_congestion_advanced_last_tick() {
  return model ? model->advanced_last_tick() : 0;
}

TESS_CONGESTION_EXPORT int tess_congestion_movement_waits_last_tick() {
  return model ? model->movement_waits_last_tick() : 0;
}

}  // extern "C"

#ifdef __EMSCRIPTEN__
int main() { return 0; }
#endif
