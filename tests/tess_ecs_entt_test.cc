#include <entt/entity/registry.hpp>
// entt before the adapter: the header requires the consumer include order.
#include <gtest/gtest.h>
#include <tess/ecs/entt/entt_adapter.h>
#include <tess/tess.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "allocation_counter.h"

namespace {

struct PassableTag {};
struct CostTag {};
struct OccupancyTag {};
struct ReservationTag {};

using Runtime2D = tess::Shape<tess::Extent3{32, 32, 1}, tess::Extent3{8, 8, 1}>;
using MovementSchema = tess::FieldSchema<
    tess::Field<PassableTag, bool>, tess::Field<CostTag, std::uint32_t>,
    tess::Field<OccupancyTag, bool>, tess::Field<ReservationTag, bool>>;
using World = tess::AlwaysResidentWorld<Runtime2D, MovementSchema>;
constexpr auto RuntimeTileCount =
    Runtime2D::size.x * Runtime2D::size.y * Runtime2D::size.z;

void fill_world(World& world) {
  for (auto& page : world.chunks()) {
    auto passable = page.template field_span<PassableTag>();
    auto cost = page.template field_span<CostTag>();
    auto occupancy = page.template field_span<OccupancyTag>();
    auto reservations = page.template field_span<ReservationTag>();
    for (std::size_t i = 0; i < passable.size(); ++i) {
      passable[i] = true;
      cost[i] = 1u;
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

// One simulation under test: registry, world, index, context, runtime.
struct Sim {
  entt::registry registry;
  World world;
  tess::TileOccupancyIndex index;
  tess::EnttPathAgentContext context;
  tess::PathRequestRuntime runtime;

  explicit Sim(std::size_t capacity = 16) {
    fill_world(world);
    index.reserve(capacity);
    context.reserve(capacity);
    reserve_runtime(runtime, capacity);
    context.tick_state.pathing_dirty = false;  // collect reports arming
  }

  auto spawn(tess::Coord3 position) -> entt::entity {
    return tess::spawn_entt_path_agent<World, OccupancyTag>(
        registry, context, world, index, position);
  }

  auto tick(std::size_t max_steps = 1) -> tess::PathAgentTickStats {
    tess::PathAgentTickOptions options;
    options.max_steps = max_steps;
    return tess::tick_entt_unit_path_agents<World, PassableTag, OccupancyTag,
                                            ReservationTag>(
        registry, context, world, runtime, index, options);
  }

  // Asserts the section-8 sync invariants for every on-board agent plus
  // the exhaustive biconditional over all tiles (valid here because
  // agents are the only occupancy writers in this fixture).
  void expect_synced() {
    std::size_t on_board = 0;
    for (const auto [entity, state, tile] :
         registry.view<tess::PathState, tess::TilePosition>().each()) {
      if (registry.all_of<tess::OffBoard>(entity)) {
        continue;
      }
      ++on_board;
      EXPECT_EQ(tile.coord, state.agent.position);
      EXPECT_EQ(index.entity_at(state.agent.position),
                tess::EnttHandleAdapter::to_handle(entity));
      EXPECT_TRUE(world.template field<OccupancyTag>(state.agent.position));
    }
    EXPECT_EQ(index.size(), on_board);
    for (std::int64_t y = 0; y < 32; ++y) {
      for (std::int64_t x = 0; x < 32; ++x) {
        const auto coord = tess::Coord3{x, y, 0};
        ASSERT_EQ(static_cast<bool>(world.template field<OccupancyTag>(coord)),
                  !index.entity_at(coord).is_null());
      }
    }
  }
};

TEST(TessEcsEntt, HandleConversionSpecialCasesNullBothDirections) {
  EXPECT_EQ(tess::EnttHandleAdapter::to_handle(entt::null),
            tess::kNullEntityHandle);
  EXPECT_EQ(tess::EnttHandleAdapter::to_entity(tess::kNullEntityHandle),
            static_cast<entt::entity>(entt::null));

  entt::registry registry;
  const auto entity = registry.create();
  const auto handle = tess::EnttHandleAdapter::to_handle(entity);
  EXPECT_FALSE(handle.is_null());
  EXPECT_EQ(tess::EnttHandleAdapter::to_entity(handle), entity);
}

TEST(TessEcsEntt, SpawnClaimsTileAndRefusesConflicts) {
  Sim sim;
  const auto first = sim.spawn(tess::Coord3{2, 2, 0});
  ASSERT_NE(first, static_cast<entt::entity>(entt::null));
  EXPECT_EQ(sim.registry.get<tess::AgentId>(first).value, 0u);
  sim.expect_synced();

  // Occupied per index/field: refused without mutation.
  EXPECT_EQ(sim.spawn(tess::Coord3{2, 2, 0}),
            static_cast<entt::entity>(entt::null));
  // Out of bounds: refused.
  EXPECT_EQ(sim.spawn(tess::Coord3{-1, 0, 0}),
            static_cast<entt::entity>(entt::null));
  // Occupied by a non-agent source (field only): refused.
  sim.world.template field<OccupancyTag>(tess::Coord3{5, 5, 0}) = true;
  EXPECT_EQ(sim.spawn(tess::Coord3{5, 5, 0}),
            static_cast<entt::entity>(entt::null));
  sim.world.template field<OccupancyTag>(tess::Coord3{5, 5, 0}) = false;

  // Spawn ids stay monotonic across refusals.
  const auto second = sim.spawn(tess::Coord3{3, 2, 0});
  ASSERT_NE(second, static_cast<entt::entity>(entt::null));
  EXPECT_EQ(sim.registry.get<tess::AgentId>(second).value, 1u);
}

TEST(TessEcsEntt, AgentsWalkToGoalsWithSyncedIndexEveryTick) {
  Sim sim;
  const auto a = sim.spawn(tess::Coord3{0, 0, 0});
  const auto b = sim.spawn(tess::Coord3{0, 4, 0});
  tess::set_entt_path_agent_goal(sim.registry, a, tess::Coord3{4, 0, 0});
  tess::set_entt_path_agent_goal(sim.registry, b, tess::Coord3{4, 4, 0});

  for (int tick = 0; tick < 4; ++tick) {
    const auto stats = sim.tick();
    EXPECT_EQ(stats.processed_paths, tick == 0);
    sim.expect_synced();
  }

  EXPECT_EQ(sim.registry.get<tess::PathState>(a).agent.position,
            (tess::Coord3{4, 0, 0}));
  EXPECT_EQ(sim.registry.get<tess::TilePosition>(b).coord,
            (tess::Coord3{4, 4, 0}));
  // Arrival consumed the PathGoal components, so quiet ticks stay quiet.
  EXPECT_FALSE(sim.registry.all_of<tess::PathGoal>(a));
  EXPECT_FALSE(sim.registry.all_of<tess::PathGoal>(b));
  const auto quiet = sim.tick();
  EXPECT_FALSE(quiet.processed_paths);
  EXPECT_EQ(quiet.movement.advanced, 0u);
}

TEST(TessEcsEntt, CollectionOrderIsIdenticalAcrossPoolChurn) {
  // Two sims run the same spawn/goal script, but the second scrambles its
  // pool packing with create/destroy churn between every step. AgentId
  // sorting must make every tick's outcome identical.
  Sim clean;
  Sim churned;

  std::vector<entt::entity> noise;
  const auto churn = [&] {
    for (int i = 0; i < 5; ++i) {
      noise.push_back(churned.registry.create());
    }
    for (std::size_t i = 0; i + 1 < noise.size(); i += 2) {
      churned.registry.destroy(noise[i]);
    }
    noise.clear();
  };

  std::vector<entt::entity> clean_agents;
  std::vector<entt::entity> churned_agents;
  for (std::int64_t i = 0; i < 4; ++i) {
    clean_agents.push_back(clean.spawn(tess::Coord3{0, i * 2, 0}));
    churn();
    churned_agents.push_back(churned.spawn(tess::Coord3{0, i * 2, 0}));
  }
  // Contended goals: all four agents converge on one column so contention
  // winners depend on processing order.
  for (std::size_t i = 0; i < 4; ++i) {
    const auto goal = tess::Coord3{2, 3, 0};
    tess::set_entt_path_agent_goal(clean.registry, clean_agents[i], goal);
    churn();
    tess::set_entt_path_agent_goal(churned.registry, churned_agents[i], goal);
  }

  for (int tick = 0; tick < 12; ++tick) {
    churn();
    const auto lhs = clean.tick();
    const auto rhs = churned.tick();
    EXPECT_EQ(lhs.processed_paths, rhs.processed_paths);
    EXPECT_EQ(lhs.pathing.completed, rhs.pathing.completed);
    EXPECT_EQ(lhs.movement.advanced, rhs.movement.advanced);
    EXPECT_EQ(lhs.movement.arrived, rhs.movement.arrived);
    EXPECT_EQ(lhs.movement.blocked_waits, rhs.movement.blocked_waits);
    for (std::size_t i = 0; i < 4; ++i) {
      EXPECT_EQ(
          clean.registry.get<tess::PathState>(clean_agents[i]).agent.position,
          churned.registry.get<tess::PathState>(churned_agents[i])
              .agent.position);
    }
  }
}

TEST(TessEcsEntt, TicketsSurviveQuietTicksAndReprocessOnlyOnRearm) {
  Sim sim;
  const auto agent = sim.spawn(tess::Coord3{0, 0, 0});
  tess::set_entt_path_agent_goal(sim.registry, agent, tess::Coord3{9, 0, 0});

  auto stats = sim.tick();
  EXPECT_TRUE(stats.processed_paths);
  EXPECT_EQ(stats.pathing.completed, 1u);
  const auto ticket_after_processing =
      sim.registry.get<tess::PathState>(agent).agent.ticket;

  // Quiet ticks keep advancing on the same ticket without re-pathing.
  for (int tick = 0; tick < 3; ++tick) {
    stats = sim.tick();
    EXPECT_FALSE(stats.processed_paths);
    EXPECT_EQ(stats.movement.advanced, 1u);
    EXPECT_EQ(sim.registry.get<tess::PathState>(agent).agent.ticket.value,
              ticket_after_processing.value);
    EXPECT_EQ(sim.registry.get<tess::PathState>(agent).agent.ticket.generation,
              ticket_after_processing.generation);
  }

  // Mid-flight reassignment: a new PathGoal re-arms exactly one more
  // processing pass, with no teleport and continuous occupancy.
  const auto before = sim.registry.get<tess::PathState>(agent).agent.position;
  tess::set_entt_path_agent_goal(sim.registry, agent, tess::Coord3{0, 9, 0});
  stats = sim.tick();
  EXPECT_TRUE(stats.processed_paths);
  const auto after = sim.registry.get<tess::PathState>(agent).agent.position;
  EXPECT_EQ(tess::manhattan_distance(before, after), 1u);
  sim.expect_synced();
  stats = sim.tick();
  EXPECT_FALSE(stats.processed_paths);
}

TEST(TessEcsEntt, ContendedTileKeepsIndexInjectiveAndBlocksLoser) {
  Sim sim;
  // Two equidistant agents race for the same tile; the tie is broken by
  // processing order, which is AgentId order, so the earlier spawn wins
  // the contended commit deterministically.
  const auto winner = sim.spawn(tess::Coord3{1, 0, 0});
  const auto loser = sim.spawn(tess::Coord3{3, 0, 0});
  tess::set_entt_path_agent_goal(sim.registry, winner, tess::Coord3{2, 0, 0});
  tess::set_entt_path_agent_goal(sim.registry, loser, tess::Coord3{2, 0, 0});

  for (int tick = 0; tick < 6; ++tick) {
    (void)sim.tick();
    sim.expect_synced();
  }

  EXPECT_EQ(sim.registry.get<tess::PathState>(winner).agent.position,
            (tess::Coord3{2, 0, 0}));
  EXPECT_FALSE(sim.registry.get<tess::PathState>(winner).agent.has_goal);
  // The loser never stole the tile: it waits Blocked beside it and retries
  // the retained next step without an occupancy-blind re-plan. This fixture
  // stops before the default bounded wait budget is exhausted.
  EXPECT_EQ(sim.registry.get<tess::PathState>(loser).agent.position,
            (tess::Coord3{3, 0, 0}));
  EXPECT_EQ(sim.registry.get<tess::PathState>(loser).agent.phase,
            tess::PathAgentPhase::Blocked);
  EXPECT_EQ(sim.index.size(), 2u);
}

TEST(TessEcsEntt, UnreachableGoalStaysTerminalUntilGoalChanges) {
  Sim sim;
  const auto agent = sim.spawn(tess::Coord3{0, 0, 0});
  // Box in the goal so it is unreachable.
  const auto goal = tess::Coord3{10, 10, 0};
  sim.world.template field<PassableTag>(tess::Coord3{9, 10, 0}) = false;
  sim.world.template field<PassableTag>(tess::Coord3{11, 10, 0}) = false;
  sim.world.template field<PassableTag>(tess::Coord3{10, 9, 0}) = false;
  sim.world.template field<PassableTag>(tess::Coord3{10, 11, 0}) = false;
  tess::set_entt_path_agent_goal(sim.registry, agent, goal);

  tess::PathAgentTickOptions options;
  options.max_blocked_retries = 2;
  const auto tick = [&] {
    return tess::tick_entt_unit_path_agents<World, PassableTag, OccupancyTag,
                                            ReservationTag>(
        sim.registry, sim.context, sim.world, sim.runtime, sim.index, options);
  };

  // First tick processes (NoPath -> Blocked); retries consume the budget;
  // the agent then turns terminally Unreachable.
  for (int i = 0; i < 4; ++i) {
    (void)tick();
  }
  EXPECT_EQ(sim.registry.get<tess::PathState>(agent).agent.phase,
            tess::PathAgentPhase::Unreachable);
  // The PathGoal component is retained but inert: further ticks do not
  // process, because the unchanged goal is not re-armed.
  EXPECT_TRUE(sim.registry.all_of<tess::PathGoal>(agent));
  auto stats = tick();
  EXPECT_FALSE(stats.processed_paths);

  // A NEW goal revives the agent.
  tess::set_entt_path_agent_goal(sim.registry, agent, tess::Coord3{4, 0, 0});
  stats = tick();
  EXPECT_TRUE(stats.processed_paths);
  EXPECT_EQ(sim.registry.get<tess::PathState>(agent).agent.phase,
            tess::PathAgentPhase::Following);
}

TEST(TessEcsEntt, TeleportRetainsGoalAndRearmsFromNewPosition) {
  Sim sim;
  const auto agent = sim.spawn(tess::Coord3{0, 0, 0});
  tess::set_entt_path_agent_goal(sim.registry, agent, tess::Coord3{8, 0, 0});
  (void)sim.tick();
  (void)sim.tick();
  ASSERT_EQ(sim.registry.get<tess::PathState>(agent).agent.phase,
            tess::PathAgentPhase::Following);

  // Mid-flight teleport: occupancy relocates atomically, the lifecycle
  // resets to Idle, and the retained PathGoal re-arms at the next tick.
  ASSERT_TRUE((tess::teleport_entt_path_agent<World, OccupancyTag>(
      sim.registry, sim.world, sim.index, agent, tess::Coord3{8, 4, 0})));
  sim.expect_synced();
  EXPECT_EQ(sim.registry.get<tess::PathState>(agent).agent.phase,
            tess::PathAgentPhase::Idle);
  EXPECT_TRUE(sim.registry.all_of<tess::PathGoal>(agent));

  auto stats = sim.tick();
  EXPECT_TRUE(stats.processed_paths);
  EXPECT_EQ(sim.registry.get<tess::PathState>(agent).agent.phase,
            tess::PathAgentPhase::Following);
  // It now walks the goal from the teleport destination.
  for (int i = 0; i < 6; ++i) {
    (void)sim.tick();
  }
  EXPECT_EQ(sim.registry.get<tess::PathState>(agent).agent.position,
            (tess::Coord3{8, 0, 0}));

  // Teleporting onto the goal tile: the retained goal is consumed through
  // the submit-time arrival path at the next processed tick.
  tess::set_entt_path_agent_goal(sim.registry, agent, tess::Coord3{12, 0, 0});
  ASSERT_TRUE((tess::teleport_entt_path_agent<World, OccupancyTag>(
      sim.registry, sim.world, sim.index, agent, tess::Coord3{12, 0, 0})));
  stats = sim.tick();
  EXPECT_EQ(stats.pathing.arrived, 1u);
  EXPECT_FALSE(sim.registry.all_of<tess::PathGoal>(agent));

  // Occupied destinations are refused without mutation.
  const auto other = sim.spawn(tess::Coord3{20, 20, 0});
  EXPECT_FALSE((tess::teleport_entt_path_agent<World, OccupancyTag>(
      sim.registry, sim.world, sim.index, other, tess::Coord3{12, 0, 0})));
  sim.expect_synced();
}

TEST(TessEcsEntt, DespawnFreesTileAndLeavesOthersTicketsValid) {
  Sim sim;
  const auto doomed = sim.spawn(tess::Coord3{0, 0, 0});
  const auto survivor = sim.spawn(tess::Coord3{0, 4, 0});
  tess::set_entt_path_agent_goal(sim.registry, doomed, tess::Coord3{6, 0, 0});
  tess::set_entt_path_agent_goal(sim.registry, survivor, tess::Coord3{6, 4, 0});
  (void)sim.tick();

  const auto doomed_tile =
      sim.registry.get<tess::PathState>(doomed).agent.position;
  ASSERT_TRUE((tess::despawn_entt_path_agent<World, OccupancyTag>(
      sim.registry, sim.world, sim.index, doomed)));
  EXPECT_FALSE(sim.registry.valid(doomed));
  EXPECT_FALSE(sim.world.template field<OccupancyTag>(doomed_tile));
  EXPECT_EQ(sim.index.entity_at(doomed_tile), tess::kNullEntityHandle);
  sim.expect_synced();

  // The survivor's ticket keeps driving quiet-tick movement to arrival.
  for (int i = 0; i < 6; ++i) {
    const auto stats = sim.tick();
    EXPECT_FALSE(stats.processed_paths);
    sim.expect_synced();
  }
  EXPECT_EQ(sim.registry.get<tess::PathState>(survivor).agent.position,
            (tess::Coord3{6, 4, 0}));
}

TEST(TessEcsEntt, ParkAndPlaceMoveAgentsAcrossTheBoardEdge) {
  Sim sim;
  const auto agent = sim.spawn(tess::Coord3{1, 1, 0});
  tess::set_entt_path_agent_goal(sim.registry, agent, tess::Coord3{5, 1, 0});
  (void)sim.tick();

  // Park: tile released, lifecycle cleared, goal retained but inert.
  ASSERT_TRUE((tess::park_entt_path_agent<World, OccupancyTag>(
      sim.registry, sim.world, sim.index, agent)));
  EXPECT_TRUE(sim.registry.all_of<tess::OffBoard>(agent));
  EXPECT_EQ(sim.index.size(), 0u);
  sim.expect_synced();
  auto stats = sim.tick();
  EXPECT_FALSE(stats.processed_paths);
  EXPECT_EQ(stats.movement.advanced, 0u);
  // Parked agents cannot teleport or park twice.
  EXPECT_FALSE((tess::park_entt_path_agent<World, OccupancyTag>(
      sim.registry, sim.world, sim.index, agent)));
  EXPECT_FALSE((tess::teleport_entt_path_agent<World, OccupancyTag>(
      sim.registry, sim.world, sim.index, agent, tess::Coord3{3, 3, 0})));

  // Place: back on the board, retained goal re-arms and completes.
  ASSERT_TRUE((tess::place_entt_path_agent<World, OccupancyTag>(
      sim.registry, sim.world, sim.index, agent, tess::Coord3{3, 1, 0})));
  EXPECT_FALSE(sim.registry.all_of<tess::OffBoard>(agent));
  sim.expect_synced();
  for (int i = 0; i < 4; ++i) {
    (void)sim.tick();
  }
  EXPECT_EQ(sim.registry.get<tess::PathState>(agent).agent.position,
            (tess::Coord3{5, 1, 0}));
  EXPECT_FALSE(sim.registry.all_of<tess::PathGoal>(agent));

  // Off-board spawn: parked from birth, placeable later.
  const auto parked =
      tess::spawn_entt_path_agent_off_board(sim.registry, sim.context);
  EXPECT_TRUE(sim.registry.all_of<tess::OffBoard>(parked));
  EXPECT_TRUE((tess::place_entt_path_agent<World, OccupancyTag>(
      sim.registry, sim.world, sim.index, parked, tess::Coord3{9, 9, 0})));
  sim.expect_synced();
}

TEST(TessEcsEntt, LifecycleIntentsRecordDeltasOnlyOnSuccess) {
  Sim sim;
  tess::DeltaCollector collector;
  collector.reserve(8, 32, 16);

  // Spawn records; a refused spawn (occupied tile) records nothing.
  const auto agent = tess::spawn_entt_path_agent<World, OccupancyTag>(
      sim.registry, sim.context, sim.world, sim.index, tess::Coord3{2, 2, 0}, 0,
      &collector);
  ASSERT_NE(agent, static_cast<entt::entity>(entt::null));
  const auto refused = tess::spawn_entt_path_agent<World, OccupancyTag>(
      sim.registry, sim.context, sim.world, sim.index, tess::Coord3{2, 2, 0}, 0,
      &collector);
  EXPECT_EQ(refused, static_cast<entt::entity>(entt::null));

  // Teleport records; a refused teleport (occupied destination) does not.
  ASSERT_TRUE((tess::teleport_entt_path_agent<World, OccupancyTag>(
      sim.registry, sim.world, sim.index, agent, tess::Coord3{4, 4, 0}, 0,
      &collector)));
  const auto blocker = sim.spawn(tess::Coord3{5, 5, 0});
  EXPECT_FALSE((tess::teleport_entt_path_agent<World, OccupancyTag>(
      sim.registry, sim.world, sim.index, agent, tess::Coord3{5, 5, 0}, 0,
      &collector)));

  // Park, place, and despawn record their kinds; a parked despawn
  // records nothing (the park already released the tile).
  ASSERT_TRUE((tess::park_entt_path_agent<World, OccupancyTag>(
      sim.registry, sim.world, sim.index, agent, 0, &collector)));
  ASSERT_TRUE((tess::place_entt_path_agent<World, OccupancyTag>(
      sim.registry, sim.world, sim.index, agent, tess::Coord3{6, 6, 0}, 0,
      &collector)));
  ASSERT_TRUE((tess::park_entt_path_agent<World, OccupancyTag>(
      sim.registry, sim.world, sim.index, agent, 0, &collector)));
  ASSERT_TRUE((tess::despawn_entt_path_agent<World, OccupancyTag>(
      sim.registry, sim.world, sim.index, agent, 0, &collector)));
  static_cast<void>(blocker);

  const auto frame = collector.publish();
  ASSERT_EQ(frame.entities.size(), 5u);
  const auto handle = frame.entities[0].entity;
  EXPECT_EQ(frame.entities[0].kind, tess::EntityDeltaKind::Spawned);
  EXPECT_EQ(frame.entities[0].to, (tess::Coord3{2, 2, 0}));
  EXPECT_EQ(frame.entities[1].kind, tess::EntityDeltaKind::Teleported);
  EXPECT_EQ(frame.entities[1].from, (tess::Coord3{2, 2, 0}));
  EXPECT_EQ(frame.entities[1].to, (tess::Coord3{4, 4, 0}));
  EXPECT_EQ(frame.entities[2].kind, tess::EntityDeltaKind::Parked);
  EXPECT_EQ(frame.entities[2].to, (tess::Coord3{4, 4, 0}));
  EXPECT_EQ(frame.entities[3].kind, tess::EntityDeltaKind::Placed);
  EXPECT_EQ(frame.entities[3].to, (tess::Coord3{6, 6, 0}));
  EXPECT_EQ(frame.entities[4].kind, tess::EntityDeltaKind::Parked);
  for (const auto& record : frame.entities) {
    EXPECT_EQ(record.entity, handle);
  }
}

TEST(TessEcsEntt, TickDriverFeedsMovesThroughTheCollector) {
  Sim sim;
  tess::DeltaCollector collector;
  collector.reserve(8, 32, 16);
  const auto agent = sim.spawn(tess::Coord3{0, 0, 0});
  tess::set_entt_path_agent_goal(sim.registry, agent, tess::Coord3{4, 0, 0});

  for (int tick = 0; tick < 4; ++tick) {
    tess::PathAgentTickOptions options;
    (void)tess::tick_entt_unit_path_agents<World, PassableTag, OccupancyTag,
                                           ReservationTag>(
        sim.registry, sim.context, sim.world, sim.runtime, sim.index, options,
        0, nullptr, &collector);
  }

  // Default coalescing folds the walk into one Moved record spanning
  // start to goal, stamped with the arrival tick.
  const auto frame = collector.publish();
  ASSERT_EQ(frame.entities.size(), 1u);
  EXPECT_EQ(frame.entities[0].kind, tess::EntityDeltaKind::Moved);
  EXPECT_EQ(frame.entities[0].from, (tess::Coord3{0, 0, 0}));
  EXPECT_EQ(frame.entities[0].to, (tess::Coord3{4, 0, 0}));
  EXPECT_EQ(frame.entities[0].last_tick, 4u);
  EXPECT_EQ(frame.header.ticks, 4u);
  EXPECT_EQ(collector.stats().moves_coalesced, 3u);
}

TEST(TessEcsEntt, SteadyStateTickIsAllocationFree) {
  Sim sim;
  const auto a = sim.spawn(tess::Coord3{0, 0, 0});
  const auto b = sim.spawn(tess::Coord3{0, 4, 0});
  tess::set_entt_path_agent_goal(sim.registry, a, tess::Coord3{31, 0, 0});
  tess::set_entt_path_agent_goal(sim.registry, b, tess::Coord3{31, 4, 0});

  // Warm: tick 1 processes paths, tick 2 exercises the movement-only
  // steady state once (and EnTT pools have seen every component).
  auto stats = sim.tick();
  ASSERT_TRUE(stats.processed_paths);
  stats = sim.tick();
  ASSERT_FALSE(stats.processed_paths);

  tess_test::ScopedAllocationCounter counter;
  for (int i = 0; i < 16; ++i) {
    stats = sim.tick();
    ASSERT_FALSE(stats.processed_paths);
    ASSERT_EQ(stats.movement.advanced, 2u);
  }
  EXPECT_EQ(counter.count(), 0u);
}

}  // namespace
