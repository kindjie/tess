#include <flecs.h>
// Flecs first: the adapter header requires the consumer include order.
#include <gtest/gtest.h>
#include <tess/ecs/flecs/flecs_adapter.h>
#include <tess/tess.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "allocation_counter.h"

namespace {

struct PassableTag {};
struct OccupancyTag {};
struct ReservationTag {};

using Runtime2D = tess::Shape<tess::Extent3{32, 32, 1}, tess::Extent3{8, 8, 1}>;
using MovementSchema = tess::FieldSchema<tess::Field<PassableTag, bool>,
                                         tess::Field<OccupancyTag, bool>,
                                         tess::Field<ReservationTag, bool>>;
using World = tess::AlwaysResidentWorld<Runtime2D, MovementSchema>;
constexpr auto RuntimeTileCount =
    Runtime2D::size.x * Runtime2D::size.y * Runtime2D::size.z;

void fill_world(World& world) {
  for (auto& page : world.chunks()) {
    auto passable = page.template field_span<PassableTag>();
    auto occupancy = page.template field_span<OccupancyTag>();
    auto reservations = page.template field_span<ReservationTag>();
    for (std::size_t i = 0; i < passable.size(); ++i) {
      passable[i] = true;
      occupancy[i] = false;
      reservations[i] = false;
    }
  }
}

void reserve_runtime(tess::PathRequestRuntime& runtime,
                     std::size_t request_count) {
  runtime.reserve_requests(request_count);
  runtime.reserve_search_nodes(RuntimeTileCount);
  runtime.reserve_path_nodes(8192);
  runtime.reserve_unit_routes(request_count);
  runtime.reserve_unit_field_products(request_count);
  runtime.reserve_unit_field_product_dependencies(World::chunk_count);
  runtime.reserve_portal_segments(request_count);
  runtime.portal_segment_cache().reserve_path_nodes(1024);
}

struct Sim {
  flecs::world ecs;
  World world;
  tess::TileOccupancyIndex index;
  tess::FlecsPathAgentContext context;
  tess::PathRequestRuntime runtime;

  explicit Sim(std::size_t capacity = 16) : context(ecs) {
    fill_world(world);
    index.reserve(capacity);
    context.reserve(capacity);
    reserve_runtime(runtime, capacity);
    context.tick_state.pathing_dirty = false;
  }

  auto spawn(tess::Coord3 position) -> flecs::entity_t {
    return tess::spawn_flecs_path_agent<World, OccupancyTag>(
        ecs, context, world, index, position);
  }

  auto tick(std::size_t max_steps = 1) -> tess::PathAgentTickStats {
    tess::PathAgentTickOptions options;
    options.max_steps = max_steps;
    return tess::tick_flecs_unit_path_agents<World, PassableTag, OccupancyTag,
                                             ReservationTag>(
        ecs, context, world, runtime, index, options);
  }

  void expect_synced() {
    std::size_t on_board = 0;
    context.agent_query.each([&](flecs::entity entity, tess::PathState& state,
                                 const tess::TilePosition& tile,
                                 const tess::AgentId&) {
      if (entity.has<tess::OffBoard>()) {
        return;
      }
      ++on_board;
      EXPECT_EQ(tile.coord, state.agent.position);
      EXPECT_EQ(index.entity_at(state.agent.position),
                tess::FlecsHandleAdapter::to_handle(entity.id()));
      EXPECT_TRUE(world.template field<OccupancyTag>(state.agent.position));
    });
    EXPECT_EQ(index.size(), on_board);
  }
};

TEST(TessEcsFlecs, HandleConversionPreservesFullGenerationAndNull) {
  EXPECT_EQ(tess::FlecsHandleAdapter::to_handle(0), tess::kNullEntityHandle);
  EXPECT_EQ(tess::FlecsHandleAdapter::to_entity(tess::kNullEntityHandle), 0u);

  flecs::world world;
  const auto first = world.entity();
  const auto old_id = first.id();
  const auto handle = tess::FlecsHandleAdapter::to_handle(old_id);
  EXPECT_EQ(tess::FlecsHandleAdapter::to_entity(handle), old_id);
  first.destruct();

  const auto recycled = world.entity();
  if (static_cast<std::uint32_t>(recycled.id()) ==
      static_cast<std::uint32_t>(old_id)) {
    EXPECT_NE(recycled.id(), old_id);
    EXPECT_NE(tess::FlecsHandleAdapter::to_handle(recycled.id()), handle);
  }
}

TEST(TessEcsFlecs, LifecycleKeepsWorldIndexAndComponentsSynchronized) {
  Sim sim;
  const auto entity = sim.spawn(tess::Coord3{2, 2, 0});
  ASSERT_NE(entity, 0u);
  EXPECT_EQ(sim.ecs.entity(entity).get<tess::AgentId>().value, 0u);
  EXPECT_EQ(sim.spawn(tess::Coord3{2, 2, 0}), 0u);
  EXPECT_EQ(sim.spawn(tess::Coord3{-1, 0, 0}), 0u);

  ASSERT_TRUE((tess::teleport_flecs_path_agent<World, OccupancyTag>(
      sim.ecs, sim.world, sim.index, entity, tess::Coord3{4, 4, 0})));
  ASSERT_TRUE((tess::park_flecs_path_agent<World, OccupancyTag>(
      sim.ecs, sim.world, sim.index, entity)));
  EXPECT_TRUE(sim.ecs.entity(entity).has<tess::OffBoard>());
  ASSERT_TRUE((tess::place_flecs_path_agent<World, OccupancyTag>(
      sim.ecs, sim.world, sim.index, entity, tess::Coord3{6, 6, 0})));
  sim.expect_synced();
  ASSERT_TRUE((tess::despawn_flecs_path_agent<World, OccupancyTag>(
      sim.ecs, sim.world, sim.index, entity)));
  EXPECT_FALSE(sim.ecs.is_alive(entity));
  EXPECT_EQ(sim.index.size(), 0u);

  const auto stale = sim.ecs.entity().id();
  sim.ecs.entity(stale).destruct();
  EXPECT_FALSE((tess::despawn_flecs_path_agent<World, OccupancyTag>(
      sim.ecs, sim.world, sim.index, stale)));
}

TEST(TessEcsFlecs, AgentsWalkAndArrivedGoalsAreConsumed) {
  Sim sim;
  const auto a = sim.spawn(tess::Coord3{0, 0, 0});
  const auto b = sim.spawn(tess::Coord3{0, 4, 0});
  tess::set_flecs_path_agent_goal(sim.ecs, a, tess::Coord3{4, 0, 0});
  tess::set_flecs_path_agent_goal(sim.ecs, b, tess::Coord3{4, 4, 0});

  for (int tick = 0; tick < 4; ++tick) {
    const auto stats = sim.tick();
    EXPECT_EQ(stats.processed_paths, tick == 0);
    sim.expect_synced();
  }

  EXPECT_EQ(sim.ecs.entity(a).get<tess::PathState>().agent.position,
            (tess::Coord3{4, 0, 0}));
  EXPECT_FALSE(sim.ecs.entity(a).has<tess::PathGoal>());
  EXPECT_FALSE(sim.ecs.entity(b).has<tess::PathGoal>());
  EXPECT_FALSE(sim.tick().processed_paths);
}

TEST(TessEcsFlecs, StableAgentIdsDefeatTableAndEntityChurn) {
  Sim clean;
  Sim churned;
  std::vector<flecs::entity_t> lhs;
  std::vector<flecs::entity_t> rhs;

  for (std::int64_t i = 0; i < 4; ++i) {
    lhs.push_back(clean.spawn(tess::Coord3{0, i * 2, 0}));
    auto noise = churned.ecs.entity().set<tess::PathGoal>(
        tess::PathGoal{tess::Coord3{1, 1, 0}});
    noise.destruct();
    rhs.push_back(churned.spawn(tess::Coord3{0, i * 2, 0}));
  }
  for (std::size_t i = 0; i < lhs.size(); ++i) {
    const auto goal = tess::Coord3{2, 3, 0};
    tess::set_flecs_path_agent_goal(clean.ecs, lhs[i], goal);
    tess::set_flecs_path_agent_goal(churned.ecs, rhs[i], goal);
  }

  for (int tick = 0; tick < 12; ++tick) {
    const auto left = clean.tick();
    const auto right = churned.tick();
    EXPECT_EQ(left.pathing.completed, right.pathing.completed);
    EXPECT_EQ(left.movement.advanced, right.movement.advanced);
    EXPECT_EQ(left.movement.blocked_waits, right.movement.blocked_waits);
    for (std::size_t i = 0; i < lhs.size(); ++i) {
      EXPECT_EQ(
          clean.ecs.entity(lhs[i]).get<tess::PathState>().agent.position,
          churned.ecs.entity(rhs[i]).get<tess::PathState>().agent.position);
    }
  }
}

TEST(TessEcsFlecs, LifecycleAndTickEmitRenderDeltas) {
  Sim sim;
  tess::DeltaCollector collector;
  collector.reserve(8, 32, 16);
  const auto entity = tess::spawn_flecs_path_agent<World, OccupancyTag>(
      sim.ecs, sim.context, sim.world, sim.index, tess::Coord3{0, 0, 0}, 0,
      &collector);
  tess::set_flecs_path_agent_goal(sim.ecs, entity, tess::Coord3{2, 0, 0});
  for (int tick = 0; tick < 2; ++tick) {
    tess::PathAgentTickOptions options;
    (void)tess::tick_flecs_unit_path_agents<World, PassableTag, OccupancyTag,
                                            ReservationTag>(
        sim.ecs, sim.context, sim.world, sim.runtime, sim.index, options, 0,
        nullptr, &collector);
  }

  const auto frame = collector.publish();
  ASSERT_EQ(frame.entities.size(), 2u);
  EXPECT_EQ(frame.entities[0].kind, tess::EntityDeltaKind::Spawned);
  EXPECT_EQ(frame.entities[1].kind, tess::EntityDeltaKind::Moved);
  EXPECT_EQ(frame.entities[1].to, (tess::Coord3{2, 0, 0}));
}

TEST(TessEcsFlecs, SteadyStateTickIsAllocationFree) {
  Sim sim;
  const auto a = sim.spawn(tess::Coord3{0, 0, 0});
  const auto b = sim.spawn(tess::Coord3{0, 4, 0});
  tess::set_flecs_path_agent_goal(sim.ecs, a, tess::Coord3{31, 0, 0});
  tess::set_flecs_path_agent_goal(sim.ecs, b, tess::Coord3{31, 4, 0});
  ASSERT_TRUE(sim.tick().processed_paths);
  ASSERT_FALSE(sim.tick().processed_paths);

  tess_test::ScopedAllocationCounter counter;
  for (int i = 0; i < 16; ++i) {
    const auto stats = sim.tick();
    ASSERT_FALSE(stats.processed_paths);
    ASSERT_EQ(stats.movement.advanced, 2u);
  }
  EXPECT_EQ(counter.count(), 0u);
}

}  // namespace
