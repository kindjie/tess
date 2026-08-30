#include "flow_steering_model.h"

#include <tess/pathfinding.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace tess::examples::web_flow_steering {
namespace {

struct PassableTag {};

using Shape = tess::Shape<tess::Extent3{width, height}, tess::Extent3{8, 8}>;
using Schema = tess::FieldSchema<tess::Field<PassableTag, std::uint8_t>>;
using World = tess::AlwaysResidentWorld<Shape, Schema>;

constexpr auto kTileCount =
    static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
constexpr tess::Coord2 kDefaultGoal{27, 4};

// North, east, south, west. The first valid descent wins every tie.
constexpr std::array<tess::Coord2, 4> kDirectionOrder{
    tess::Coord2{0, -1},
    tess::Coord2{1, 0},
    tess::Coord2{0, 1},
    tess::Coord2{-1, 0},
};

[[nodiscard]] auto in_bounds(int x, int y) noexcept -> bool {
  return x >= 0 && x < width && y >= 0 && y < height;
}

[[nodiscard]] auto coord(int x, int y) noexcept -> tess::Coord2 {
  return {x, y};
}

struct Agent {
  tess::Coord2 position{};
  AgentState state = AgentState::Moving;
};

constexpr std::array<tess::Coord2, max_agents> kAgentStarts{
    tess::Coord2{2, 2},
    tess::Coord2{2, 2},  // Deliberate overlap: guidance is not avoidance.
    tess::Coord2{2, 21},
    tess::Coord2{15, 5},
    kDefaultGoal,          // Demonstrates the at-goal hold state.
    tess::Coord2{28, 20},  // Sealed pocket: demonstrates unreachable.
    tess::Coord2{10, 14},
    tess::Coord2{20, 18},
};

}  // namespace

struct FlowSteeringModel::Impl {
  Impl() {
    scratch.reserve_nodes(kTileCount);
    product.reserve_goals(1);
    product.reserve_nodes(kTileCount);
    product.reserve_dependencies(World::chunk_count);
  }

  void set_wall(int x, int y) { world.field<PassableTag>(coord(x, y)) = 0; }

  void build_terrain() {
    world.fill_field<PassableTag>(1);
    for (int y = 1; y < height - 1; ++y) {
      if (y != 5 && y != 18) {
        set_wall(8, y);
      }
    }
    for (int y = 0; y < 21; ++y) {
      if (y != 9) {
        set_wall(16, y);
      }
    }
    for (int y = 3; y < height; ++y) {
      if (y != 15) {
        set_wall(24, y);
      }
    }

    // A passable cell enclosed by four walls remains in the world but cannot
    // reach an outside goal. Its agent must hold rather than improvise.
    set_wall(28, 19);
    set_wall(29, 20);
    set_wall(28, 21);
    set_wall(27, 20);
  }

  void classify_agents() {
    for (auto& agent : agents) {
      const auto distance = product.distance_at<World>(agent.position);
      if (agent.position == goal) {
        agent.state = AgentState::AtGoal;
      } else if (distance == tess::DistanceFieldProduct::unreachable_distance) {
        agent.state = AgentState::Unreachable;
      } else {
        agent.state = AgentState::Moving;
      }
    }
  }

  [[nodiscard]] auto rebuild() -> bool {
    goals.clear();
    goals.add(goal);
    const auto result = tess::build_distance_field_product<World, PassableTag>(
        world, goals, product, scratch);
    ++generation;
    classify_agents();
    return result.status == tess::PathStatus::Found;
  }

  World world;
  tess::GoalSet goals;
  tess::DistanceFieldScratch scratch;
  tess::DistanceFieldProduct product;
  std::array<Agent, max_agents> agents{};
  tess::Coord2 goal = kDefaultGoal;
  std::uint32_t generation = 0;
  std::uint32_t steps = 0;
};

FlowSteeringModel::FlowSteeringModel() : impl_(std::make_unique<Impl>()) {
  reset();
}

FlowSteeringModel::~FlowSteeringModel() = default;

void FlowSteeringModel::reset() {
  impl_->build_terrain();
  for (std::size_t index = 0; index < impl_->agents.size(); ++index) {
    impl_->agents[index] = Agent{kAgentStarts[index], AgentState::Moving};
  }
  impl_->goal = kDefaultGoal;
  impl_->steps = 0;
  (void)impl_->rebuild();
}

auto FlowSteeringModel::set_goal(int x, int y) -> bool {
  if (!in_bounds(x, y) || !tile_passable(x, y)) {
    return false;
  }
  impl_->goal = coord(x, y);
  return impl_->rebuild();
}

auto FlowSteeringModel::tick() -> int {
  auto moved = 0;
  // [flow-steering-descent]
  for (auto& agent : impl_->agents) {
    const auto current_distance =
        impl_->product.distance_at<World>(agent.position);
    if (current_distance == 0) {
      agent.state = AgentState::AtGoal;
      continue;
    }
    if (current_distance == tess::DistanceFieldProduct::unreachable_distance) {
      agent.state = AgentState::Unreachable;
      continue;
    }

    auto descended = false;
    for (const auto direction : kDirectionOrder) {
      const auto neighbor = tess::Coord2{
          agent.position.x + direction.x,
          agent.position.y + direction.y,
      };
      if (!in_bounds(static_cast<int>(neighbor.x),
                     static_cast<int>(neighbor.y)) ||
          impl_->world.field<PassableTag>(neighbor) == 0) {
        continue;
      }
      const auto neighbor_distance =
          impl_->product.distance_at<World>(neighbor);
      if (neighbor_distance == current_distance - 1) {
        agent.position = neighbor;
        agent.state =
            neighbor_distance == 0 ? AgentState::AtGoal : AgentState::Moving;
        ++moved;
        descended = true;
        break;
      }
    }
    if (!descended) {
      agent.state = AgentState::Unreachable;
    }
  }
  // [flow-steering-descent]
  ++impl_->steps;
  return moved;
}

auto FlowSteeringModel::goal_x() const noexcept -> int {
  return static_cast<int>(impl_->goal.x);
}

auto FlowSteeringModel::goal_y() const noexcept -> int {
  return static_cast<int>(impl_->goal.y);
}

auto FlowSteeringModel::agent_count() const noexcept -> int {
  return static_cast<int>(impl_->agents.size());
}

auto FlowSteeringModel::agent_x(int index) const noexcept -> int {
  if (index < 0 || index >= agent_count()) {
    return -1;
  }
  return static_cast<int>(
      impl_->agents[static_cast<std::size_t>(index)].position.x);
}

auto FlowSteeringModel::agent_y(int index) const noexcept -> int {
  if (index < 0 || index >= agent_count()) {
    return -1;
  }
  return static_cast<int>(
      impl_->agents[static_cast<std::size_t>(index)].position.y);
}

auto FlowSteeringModel::agent_state(int index) const noexcept -> AgentState {
  if (index < 0 || index >= agent_count()) {
    return AgentState::Unreachable;
  }
  return impl_->agents[static_cast<std::size_t>(index)].state;
}

auto FlowSteeringModel::tile_passable(int x, int y) const noexcept -> bool {
  return in_bounds(x, y) && impl_->world.field<PassableTag>(coord(x, y)) != 0;
}

auto FlowSteeringModel::tile_distance(int x, int y) const noexcept
    -> std::uint32_t {
  if (!in_bounds(x, y)) {
    return tess::DistanceFieldProduct::unreachable_distance;
  }
  return impl_->product.distance_at<World>(coord(x, y));
}

auto FlowSteeringModel::build_generation() const noexcept -> std::uint32_t {
  return impl_->generation;
}

auto FlowSteeringModel::step_count() const noexcept -> std::uint32_t {
  return impl_->steps;
}

}  // namespace tess::examples::web_flow_steering
