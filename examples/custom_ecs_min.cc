// Minimal custom-ECS adapter: proves the tess::ecs concepts are not
// EnTT-shaped by driving the generic tick_ecs_* pipeline from a
// hand-rolled store -- parallel arrays, generational ids, and the game's
// own position component. No third-party ECS anywhere.
#include <tess/tess.h>

#include <algorithm>
#include <cstdint>
#include <exception>
#include <iostream>
#include <optional>
#include <vector>

namespace {

struct PassableTag {};
struct CostTag {};
struct OccupancyTag {};
struct ReservationTag {};

using Shape = tess::Shape<tess::Extent3{32, 32, 1}, tess::Extent3{8, 8, 1}>;
using Schema = tess::FieldSchema<
    tess::Field<PassableTag, bool>, tess::Field<CostTag, std::uint32_t>,
    tess::Field<OccupancyTag, bool>, tess::Field<ReservationTag, bool>>;
using World = tess::AlwaysResidentWorld<Shape, Schema>;

// --- The game's own ECS: one slot per entity, components as parallel
// arrays. Entities are (index, generation) pairs so stale references
// never resurrect a reused slot.

struct MiniEntity {
  std::uint32_t index = 0;
  std::uint32_t generation = 0;
};

struct MiniEcs {
  std::vector<std::uint32_t> generations;
  std::vector<bool> alive;
  std::vector<std::uint64_t> spawn_ids;  // tess::AgentId equivalent
  std::vector<tess::Coord3> positions;   // the game's position component
  std::vector<std::optional<tess::Coord3>> goals;
  std::vector<tess::PathAgentState> agents;
  std::uint64_t next_spawn_id = 0;

  auto create(tess::Coord3 position) -> MiniEntity {
    const auto index = static_cast<std::uint32_t>(alive.size());
    generations.push_back(0);
    alive.push_back(true);
    spawn_ids.push_back(next_spawn_id++);
    positions.push_back(position);
    goals.emplace_back();
    auto agent = tess::PathAgentState{};
    agent.position = position;
    agents.push_back(agent);
    return MiniEntity{index, 0};
  }
};

// The two documented adapter concepts, implemented over MiniEcs.

struct MiniHandleAdapter {
  using entity_type = MiniEntity;

  [[nodiscard]] static auto to_handle(MiniEntity entity) noexcept
      -> tess::EntityHandle {
    return tess::EntityHandle{
        (static_cast<std::uint64_t>(entity.generation) << 32U) | entity.index};
  }

  [[nodiscard]] static auto to_entity(tess::EntityHandle handle) noexcept
      -> MiniEntity {
    return MiniEntity{static_cast<std::uint32_t>(handle.value),
                      static_cast<std::uint32_t>(handle.value >> 32U)};
  }
};
static_assert(tess::EntityHandleAdapter<MiniHandleAdapter>);

struct MiniPositionAdapter {
  MiniEcs* ecs = nullptr;

  [[nodiscard]] auto position(const MiniEntity& entity) const -> tess::Coord3 {
    return ecs->positions[entity.index];
  }

  void set_position(const MiniEntity& entity, tess::Coord3 coord) {
    ecs->positions[entity.index] = coord;
  }
};
static_assert(tess::PositionAdapter<MiniPositionAdapter, MiniEntity>);

// Source + sink over the store. Collection sorts by spawn id -- the
// deterministic-order contract -- and reconciles the goal component into
// the lifecycle; application mirrors state back and consumes goals on
// arrival, exactly like the EnTT adapter but over plain arrays.
struct MiniAgentSystem {
  MiniEcs* ecs = nullptr;
  std::vector<std::uint32_t> collected;  // slot indices, spawn-id order

  auto collect(tess::PathAgentBatch& batch) -> tess::PathAgentCollectInfo {
    batch.clear();
    collected.clear();
    for (std::uint32_t i = 0; i < ecs->alive.size(); ++i) {
      if (ecs->alive[i]) {
        collected.push_back(i);
      }
    }
    std::sort(collected.begin(), collected.end(),
              [this](std::uint32_t lhs, std::uint32_t rhs) {
                return ecs->spawn_ids[lhs] < ecs->spawn_ids[rhs];
              });

    tess::PathAgentCollectInfo info;
    info.count = collected.size();
    for (const auto slot : collected) {
      auto& agent = ecs->agents[slot];
      if (ecs->goals[slot].has_value()) {
        if (!agent.has_goal || agent.goal != *ecs->goals[slot]) {
          tess::set_path_agent_goal(agent, *ecs->goals[slot]);
          info.pathing_dirty = true;
        }
      } else if (agent.has_goal) {
        tess::clear_path_agent_goal(agent);
      }
      batch.push(MiniHandleAdapter::to_handle(
                     MiniEntity{slot, ecs->generations[slot]}),
                 agent);
    }
    return info;
  }

  void apply(const tess::PathAgentBatch& batch) {
    MiniPositionAdapter positions{ecs};
    const auto agents = batch.agents();
    for (std::size_t i = 0; i < collected.size(); ++i) {
      const auto slot = collected[i];
      const auto& agent = agents[i];
      ecs->agents[slot] = agent;
      positions.set_position(MiniEntity{slot, ecs->generations[slot]},
                             agent.position);
      if (ecs->goals[slot].has_value() && !agent.has_goal &&
          agent.position == *ecs->goals[slot]) {
        ecs->goals[slot].reset();  // consume on arrival
      }
    }
  }
};
static_assert(tess::PathAgentSource<MiniAgentSystem>);
static_assert(tess::PathAgentSink<MiniAgentSystem>);

auto run() -> int {
  World world;
  for (auto& page : world.chunks()) {
    auto passable = page.template field_span<PassableTag>();
    auto cost = page.template field_span<CostTag>();
    for (std::size_t i = 0; i < passable.size(); ++i) {
      passable[i] = true;
      cost[i] = 1u;
    }
  }

  MiniEcs ecs;
  tess::TileOccupancyIndex index;
  index.reserve(8);
  tess::PathAgentBatch batch;
  batch.reserve(8);

  for (std::int64_t i = 0; i < 3; ++i) {
    const auto position = tess::Coord3{0, i * 3, 0};
    const auto entity = ecs.create(position);
    world.field<OccupancyTag>(position) = true;
    if (!index.insert(position, MiniHandleAdapter::to_handle(entity))) {
      std::cerr << "occupancy index rejected a fresh spawn\n";
      return 1;
    }
    ecs.goals[entity.index] = tess::Coord3{6, i * 3, 0};
  }

  tess::PathRequestRuntime runtime;
  runtime.reserve_requests(ecs.agents.size());
  runtime.reserve_search_nodes(Shape::size.x * Shape::size.y * Shape::size.z);
  runtime.reserve_path_nodes(4096);
  runtime.reserve_unit_routes(ecs.agents.size());

  tess::PathAgentTickState tick_state;
  tick_state.pathing_dirty = false;  // collect reports goal arming
  MiniAgentSystem system{&ecs, {}};

  for (int tick = 0; tick < 32; ++tick) {
    const auto stats =
        tess::tick_ecs_unit_path_agents<World, PassableTag, OccupancyTag,
                                        ReservationTag>(
            tick_state, world, system, system, batch, runtime, index);
    std::cout << "tick " << stats.tick << ": advanced "
              << stats.movement.advanced << ", arrived "
              << stats.movement.arrived
              << (stats.processed_paths ? " (processed paths)" : "") << "\n";
    if (stats.movement.advanced == 0 && !stats.processed_paths) {
      break;
    }
  }

  for (std::uint32_t slot = 0; slot < ecs.agents.size(); ++slot) {
    const auto position = ecs.positions[slot];
    const auto handle = index.entity_at(position);
    const auto goal_row = static_cast<std::int64_t>(slot) * 3;
    if (position != tess::Coord3{6, goal_row, 0} ||
        MiniHandleAdapter::to_entity(handle).index != slot) {
      std::cerr << "pawn " << slot << " out of sync\n";
      return 1;
    }
  }
  std::cout << "all pawns arrived; tile->entity index in sync\n";
  return 0;
}

}  // namespace

auto main() -> int {
  try {
    return run();
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << "\n";
    return 1;
  }
}
