#include <array>
#include <cstdint>
#include <cstdio>

#include "flow_steering_model.h"

namespace flow = tess::examples::web_flow_steering;

namespace {

[[nodiscard]] auto check_legal_descent() -> bool {
  flow::FlowSteeringModel model;
  std::array<int, flow::max_agents> old_x{};
  std::array<int, flow::max_agents> old_y{};
  std::array<std::uint32_t, flow::max_agents> old_distance{};
  int moving = 0;
  for (int index = 0; index < model.agent_count(); ++index) {
    if (model.agent_state(index) != flow::AgentState::Moving) {
      continue;
    }
    ++moving;
    const auto slot = static_cast<std::size_t>(index);
    old_x[slot] = model.agent_x(index);
    old_y[slot] = model.agent_y(index);
    old_distance[slot] = model.tile_distance(old_x[slot], old_y[slot]);
  }
  (void)model.tick();
  for (int index = 0; index < model.agent_count(); ++index) {
    const auto slot = static_cast<std::size_t>(index);
    if (old_distance[slot] == 0) {
      continue;
    }
    const auto new_x = model.agent_x(index);
    const auto new_y = model.agent_y(index);
    const auto new_distance = model.tile_distance(new_x, new_y);
    const auto x_step =
        new_x > old_x[slot] ? new_x - old_x[slot] : old_x[slot] - new_x;
    const auto y_step =
        new_y > old_y[slot] ? new_y - old_y[slot] : old_y[slot] - new_y;
    if (x_step + y_step != 1 || new_distance + 1 != old_distance[slot] ||
        !model.tile_passable(new_x, new_y)) {
      return false;
    }
  }
  return moving > 0;
}

[[nodiscard]] auto check_goal_and_unreachable_hold() -> bool {
  flow::FlowSteeringModel model;
  int at_goal = -1;
  int unreachable = -1;
  for (int index = 0; index < model.agent_count(); ++index) {
    if (model.agent_state(index) == flow::AgentState::AtGoal) {
      at_goal = index;
    }
    if (model.agent_state(index) == flow::AgentState::Unreachable) {
      unreachable = index;
    }
  }
  if (at_goal < 0 || unreachable < 0) {
    return false;
  }
  const auto goal_x = model.agent_x(at_goal);
  const auto goal_y = model.agent_y(at_goal);
  const auto unreachable_x = model.agent_x(unreachable);
  const auto unreachable_y = model.agent_y(unreachable);
  for (int tick = 0; tick < 12; ++tick) {
    (void)model.tick();
  }
  return model.agent_x(at_goal) == goal_x && model.agent_y(at_goal) == goal_y &&
         model.agent_x(unreachable) == unreachable_x &&
         model.agent_y(unreachable) == unreachable_y;
}

[[nodiscard]] auto check_synchronous_rebuild() -> bool {
  flow::FlowSteeringModel model;
  const auto generation = model.build_generation();
  if (!model.set_goal(3, 12) || model.build_generation() != generation + 1 ||
      model.tile_distance(3, 12) != 0 || model.goal_x() != 3 ||
      model.goal_y() != 12) {
    return false;
  }
  const auto after_valid = model.build_generation();
  return !model.set_goal(8, 1) && model.build_generation() == after_valid;
}

[[nodiscard]] auto check_determinism() -> bool {
  flow::FlowSteeringModel first;
  flow::FlowSteeringModel second;
  for (int tick = 0; tick < 48; ++tick) {
    if (first.tick() != second.tick()) {
      return false;
    }
    for (int index = 0; index < first.agent_count(); ++index) {
      if (first.agent_x(index) != second.agent_x(index) ||
          first.agent_y(index) != second.agent_y(index) ||
          first.agent_state(index) != second.agent_state(index)) {
        return false;
      }
    }
  }
  return first.agent_x(0) == first.agent_x(1) &&
         first.agent_y(0) == first.agent_y(1);
}

}  // namespace

int main() {
  const auto ok = check_determinism() && check_legal_descent() &&
                  check_goal_and_unreachable_hold() &&
                  check_synchronous_rebuild();
  std::puts(ok ? "flow steering model: ok" : "flow steering model: failed");
  return ok ? 0 : 1;
}
