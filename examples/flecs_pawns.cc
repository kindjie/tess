// Flecs pawn movement through the tess ECS adapter: spawn pawns as
// entities, drive them with PathGoal components, reassign one goal
// mid-flight, and despawn a pawn to free its tile -- all through the
// sanctioned lifecycle intents. Requires TESS_ENABLE_FLECS and Flecs
// (both provided by the example's build target).
#include <flecs.h>
// Flecs first: the adapter header requires the consumer include order.
#include <tess/ecs/flecs/flecs_adapter.h>
#include <tess/tess.h>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>

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

  flecs::world ecs;
  tess::FlecsPathAgentContext context(ecs);
  tess::TileOccupancyIndex index;
  constexpr std::size_t kPawns = 8;
  context.reserve(kPawns);
  index.reserve(kPawns);
  context.tick_state.pathing_dirty = false;  // collect reports goal arming

  flecs::entity_t pawns[kPawns];
  for (std::size_t i = 0; i < kPawns; ++i) {
    const auto row = static_cast<std::int64_t>(i) * 2;
    pawns[i] = tess::spawn_flecs_path_agent<World, OccupancyTag>(
        ecs, context, world, index, tess::Coord3{0, row, 0});
    if (pawns[i] == 0u) {
      std::cerr << "spawn refused for pawn " << i << "\n";
      return 1;
    }
    tess::set_flecs_path_agent_goal(ecs, pawns[i], tess::Coord3{12, row, 0});
  }

  tess::PathRequestRuntime runtime;
  runtime.reserve_requests(kPawns);
  runtime.reserve_search_nodes(Shape::size.x * Shape::size.y * Shape::size.z);
  runtime.reserve_path_nodes(4096);
  runtime.reserve_unit_routes(kPawns);

  for (int tick = 0; tick < 40; ++tick) {
    if (tick == 3) {
      // Mid-flight reassignment: writing a new PathGoal re-arms the
      // pawn from wherever it stands -- no teleport, no identity churn.
      tess::set_flecs_path_agent_goal(ecs, pawns[0], tess::Coord3{0, 15, 0});
      std::cout << "tick 3: pawn 0 rerouted mid-flight\n";
    }
    if (tick == 5) {
      // Despawn releases the pawn's tile; nobody else's route breaks.
      const auto tile =
          ecs.entity(pawns[1]).get<tess::PathState>().agent.position;
      if (!tess::despawn_flecs_path_agent<World, OccupancyTag>(
              ecs, world, index, pawns[1])) {
        std::cerr << "despawn refused\n";
        return 1;
      }
      std::cout << "tick 5: pawn 1 despawned, tile (" << tile.x << ", "
                << tile.y << ") freed: "
                << (index.entity_at(tile).is_null() ? "yes" : "no") << "\n";
    }

    const auto stats =
        tess::tick_flecs_unit_path_agents<World, PassableTag, OccupancyTag,
                                          ReservationTag>(ecs, context, world,
                                                          runtime, index);
    std::cout << "tick " << stats.tick << ": advanced "
              << stats.movement.advanced << ", arrived "
              << stats.movement.arrived
              << (stats.processed_paths ? " (processed paths)" : "") << "\n";
    if (tick > 5 && stats.movement.advanced == 0 && !stats.processed_paths) {
      break;
    }
  }

  std::size_t settled = 0;
  bool invalid = false;
  context.agent_query.each([&](flecs::entity entity, tess::PathState& state,
                               const tess::TilePosition&,
                               const tess::AgentId&) {
    if (entity.has<tess::OffBoard>()) {
      return;
    }
    if (state.agent.phase != tess::PathAgentPhase::Idle) {
      std::cerr << "a pawn never settled\n";
      invalid = true;
    }
    if (index.entity_at(state.agent.position) !=
        tess::FlecsHandleAdapter::to_handle(entity.id())) {
      std::cerr << "tile->entity index out of sync\n";
      invalid = true;
    }
    ++settled;
  });
  if (invalid) {
    return 1;
  }
  std::cout << settled << " pawns settled; tile->entity index in sync\n";
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
