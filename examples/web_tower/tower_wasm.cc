#include "tower_model.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define TESS_TOWER_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define TESS_TOWER_EXPORT
#endif

#include <cstdint>
#include <memory>

namespace demo = tess::examples::web_tower;

namespace {

std::unique_ptr<demo::TowerModel> model;

}  // namespace

extern "C" {

TESS_TOWER_EXPORT int tess_tower_width() { return demo::width; }
TESS_TOWER_EXPORT int tess_tower_depth() { return demo::depth; }
TESS_TOWER_EXPORT int tess_tower_floors() { return demo::floors; }
TESS_TOWER_EXPORT int tess_tower_max_agents() { return demo::max_agents; }

TESS_TOWER_EXPORT int tess_tower_reset(int agent_count) {
  model = std::make_unique<demo::TowerModel>(agent_count);
  return model->agent_count();
}

TESS_TOWER_EXPORT double tess_tower_tick(double dt_seconds) {
  return model ? model->tick(dt_seconds) : 0.0;
}

TESS_TOWER_EXPORT int tess_tower_relaunch() {
  return model ? model->relaunch() : 0;
}

TESS_TOWER_EXPORT int tess_tower_set_stairwell(int index, int open) {
  return model && model->set_stairwell(index, open != 0) ? 1 : 0;
}

TESS_TOWER_EXPORT int tess_tower_stairwell_count() {
  return model ? model->stairwell_count() : 0;
}

TESS_TOWER_EXPORT int tess_tower_stairwell_open(int index) {
  return model && model->stairwell_open(index) ? 1 : 0;
}

TESS_TOWER_EXPORT int tess_tower_stairwell_x(int index) {
  return model ? model->stairwell_x(index) : 0;
}

TESS_TOWER_EXPORT int tess_tower_stairwell_y(int index) {
  return model ? model->stairwell_y(index) : 0;
}

TESS_TOWER_EXPORT int tess_tower_agent_count() {
  return model ? model->agent_count() : 0;
}
TESS_TOWER_EXPORT int tess_tower_arrived() {
  return model ? model->arrived() : 0;
}
TESS_TOWER_EXPORT int tess_tower_crowd_blocked() {
  return model ? model->crowd_blocked() : 0;
}
TESS_TOWER_EXPORT int tess_tower_unreachable() {
  return model ? model->unreachable() : 0;
}
TESS_TOWER_EXPORT int tess_tower_climbing() {
  return model ? model->climbing() : 0;
}
TESS_TOWER_EXPORT int tess_tower_leg() { return model ? model->leg() : 0; }
TESS_TOWER_EXPORT int tess_tower_completed_legs() {
  return model ? model->completed_legs() : 0;
}
TESS_TOWER_EXPORT int tess_tower_turnaround_ready() {
  return model && model->turnaround_ready() ? 1 : 0;
}
TESS_TOWER_EXPORT int tess_tower_advanced_last_tick() {
  return model ? model->advanced_last_tick() : 0;
}

TESS_TOWER_EXPORT const std::uint8_t* tess_tower_tiles() {
  return model ? model->tiles() : nullptr;
}
TESS_TOWER_EXPORT const std::int16_t* tess_tower_agents() {
  return model ? model->current_agents() : nullptr;
}
TESS_TOWER_EXPORT const std::int16_t* tess_tower_previous_agents() {
  return model ? model->previous_agents() : nullptr;
}
TESS_TOWER_EXPORT double tess_tower_interpolation_alpha() {
  return model ? model->interpolation_alpha() : 0.0;
}

}  // extern "C"
