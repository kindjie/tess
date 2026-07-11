#include <gtest/gtest.h>
#include <tess/tess.h>

#include <array>
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

// ---- concept validation against a deliberately non-ECS store ----

struct FakeEntity {
  std::uint32_t id = 0;
};

struct FakeHandleAdapter {
  using entity_type = FakeEntity;
  [[nodiscard]] static auto to_handle(FakeEntity entity) noexcept
      -> tess::EntityHandle {
    return tess::EntityHandle{entity.id};
  }
  [[nodiscard]] static auto to_entity(tess::EntityHandle handle) noexcept
      -> FakeEntity {
    return FakeEntity{static_cast<std::uint32_t>(handle.value)};
  }
};
static_assert(tess::EntityHandleAdapter<FakeHandleAdapter>);

struct FakePositionStore {
  std::vector<tess::Coord3> coords;
  [[nodiscard]] auto position(FakeEntity entity) const -> tess::Coord3 {
    return coords[entity.id];
  }
  void set_position(FakeEntity entity, tess::Coord3 coord) {
    coords[entity.id] = coord;
  }
};
static_assert(tess::PositionAdapter<FakePositionStore, FakeEntity>);

// Plain-array agent store: entries stay sorted by spawn id (they are
// appended with ascending ids and never reordered), so collection order
// is deterministic by construction.
struct FakeStore {
  struct Entry {
    std::uint64_t agent_id = 0;
    tess::EntityHandle handle{};
    tess::PathAgentState agent{};
    bool off_board = false;
  };

  std::vector<Entry> entries;
  bool goals_dirty = false;

  void spawn(std::uint64_t agent_id, tess::EntityHandle handle,
             tess::Coord3 position) {
    Entry entry;
    entry.agent_id = agent_id;
    entry.handle = handle;
    entry.agent.position = position;
    entries.push_back(entry);
  }

  void set_goal(std::size_t entry_index, tess::Coord3 goal) {
    tess::set_path_agent_goal(entries[entry_index].agent, goal);
    goals_dirty = true;
  }

  auto collect(tess::PathAgentBatch& batch) -> tess::PathAgentCollectInfo {
    batch.clear();
    tess::PathAgentCollectInfo info;
    for (const auto& entry : entries) {
      if (entry.off_board) {
        continue;
      }
      batch.push(entry.handle, entry.agent);
      ++info.count;
    }
    info.pathing_dirty = goals_dirty;
    goals_dirty = false;
    return info;
  }

  void apply(const tess::PathAgentBatch& batch) {
    const auto agents = batch.agents();
    std::size_t batch_index = 0;
    for (auto& entry : entries) {
      if (entry.off_board) {
        continue;
      }
      entry.agent = agents[batch_index];
      ++batch_index;
    }
  }
};
static_assert(tess::PathAgentSource<FakeStore>);
static_assert(tess::PathAgentSink<FakeStore>);

void install_agent(World& world, tess::TileOccupancyIndex& index,
                   tess::EntityHandle handle, tess::Coord3 position) {
  world.template field<OccupancyTag>(position) = true;
  ASSERT_TRUE(index.insert(position, handle));
}

// Asserts the position/index/field synchronization invariants for one
// on-board agent.
void expect_agent_synced(const World& world,
                         const tess::TileOccupancyIndex& index,
                         tess::EntityHandle handle,
                         const tess::PathAgentState& agent) {
  EXPECT_EQ(index.entity_at(agent.position), handle);
  EXPECT_TRUE(world.template field<OccupancyTag>(agent.position));
}

TEST(TessEcsAdapter, EntityHandleDefaultsToNullAndCompares) {
  const tess::EntityHandle null_handle;
  EXPECT_TRUE(null_handle.is_null());
  EXPECT_EQ(null_handle, tess::kNullEntityHandle);

  const tess::EntityHandle live{42};
  EXPECT_FALSE(live.is_null());
  EXPECT_NE(live, null_handle);
  EXPECT_EQ(live, (tess::EntityHandle{42}));
}

TEST(TessEcsAdapter, OccupancyIndexInsertEraseMoveBasics) {
  tess::TileOccupancyIndex index;
  index.reserve(8);
  const tess::EntityHandle a{1};
  const tess::EntityHandle b{2};

  EXPECT_TRUE(index.insert(tess::Coord3{1, 2, 3}, a));
  EXPECT_EQ(index.entity_at(tess::Coord3{1, 2, 3}), a);
  EXPECT_EQ(index.size(), 1u);

  // Re-inserting the identical mapping is idempotent.
  EXPECT_TRUE(index.insert(tess::Coord3{1, 2, 3}, a));
  EXPECT_EQ(index.size(), 1u);

  // A different entity is refused without mutation.
  EXPECT_FALSE(index.insert(tess::Coord3{1, 2, 3}, b));
  EXPECT_EQ(index.entity_at(tess::Coord3{1, 2, 3}), a);
  EXPECT_EQ(index.size(), 1u);

  EXPECT_TRUE(index.insert(tess::Coord3{4, 0, 0}, b));
  index.move(tess::Coord3{1, 2, 3}, tess::Coord3{2, 2, 3}, a);
  EXPECT_EQ(index.entity_at(tess::Coord3{1, 2, 3}), tess::kNullEntityHandle);
  EXPECT_EQ(index.entity_at(tess::Coord3{2, 2, 3}), a);
  EXPECT_EQ(index.size(), 2u);

  EXPECT_EQ(index.erase(tess::Coord3{2, 2, 3}), a);
  EXPECT_EQ(index.erase(tess::Coord3{2, 2, 3}), tess::kNullEntityHandle);
  EXPECT_EQ(index.size(), 1u);

  index.clear();
  EXPECT_EQ(index.size(), 0u);
  EXPECT_EQ(index.entity_at(tess::Coord3{4, 0, 0}), tess::kNullEntityHandle);
  // The cleared table stays usable.
  EXPECT_TRUE(index.insert(tess::Coord3{4, 0, 0}, b));
  EXPECT_EQ(index.entity_at(tess::Coord3{4, 0, 0}), b);
}

TEST(TessEcsAdapter, OccupancyIndexEraseKeepsProbeChainsIntact) {
  // Backward-shift deletion regression: pack the table densely enough
  // that probe chains are guaranteed (256 keys in a 512-slot table with
  // a deterministic hash), then erase interleaved keys and verify every
  // survivor stays reachable after each erasure.
  tess::TileOccupancyIndex index;
  constexpr std::int64_t kCount = 256;
  index.reserve(kCount);

  const auto coord_for = [](std::int64_t i) {
    return tess::Coord3{i % 16, i / 16, 0};
  };
  for (std::int64_t i = 0; i < kCount; ++i) {
    ASSERT_TRUE(index.insert(
        coord_for(i), tess::EntityHandle{static_cast<std::uint64_t>(i)}));
  }

  std::vector<bool> present(kCount, true);
  const auto verify_all = [&] {
    for (std::int64_t i = 0; i < kCount; ++i) {
      const auto found = index.entity_at(coord_for(i));
      if (present[static_cast<std::size_t>(i)]) {
        ASSERT_EQ(found.value, static_cast<std::uint64_t>(i));
      } else {
        ASSERT_TRUE(found.is_null());
      }
    }
  };

  // Erase every other key, verifying the full survivor set each time so
  // a single severed probe chain fails immediately.
  for (std::int64_t i = 0; i < kCount; i += 2) {
    ASSERT_EQ(index.erase(coord_for(i)).value, static_cast<std::uint64_t>(i));
    present[static_cast<std::size_t>(i)] = false;
    verify_all();
  }
  EXPECT_EQ(index.size(), static_cast<std::size_t>(kCount) / 2);

  // Erase the rest in reverse to shift chains from the other side.
  for (std::int64_t i = kCount - 1; i >= 0; i -= 2) {
    ASSERT_EQ(index.erase(coord_for(i)).value, static_cast<std::uint64_t>(i));
    present[static_cast<std::size_t>(i)] = false;
    verify_all();
  }
  EXPECT_EQ(index.size(), 0u);
}

TEST(TessEcsAdapter, OccupancyIndexAndBatchAreAllocationFreeAfterReserve) {
  tess::TileOccupancyIndex index;
  index.reserve(64);
  tess::PathAgentBatch batch;
  batch.reserve(64);

  tess_test::ScopedAllocationCounter counter;
  for (std::int64_t i = 0; i < 64; ++i) {
    ASSERT_TRUE(
        index.insert(tess::Coord3{i, 0, 0},
                     tess::EntityHandle{static_cast<std::uint64_t>(i) + 1}));
  }
  for (std::int64_t i = 0; i < 64; ++i) {
    index.move(tess::Coord3{i, 0, 0}, tess::Coord3{i, 1, 0},
               tess::EntityHandle{static_cast<std::uint64_t>(i) + 1});
  }
  for (std::int64_t i = 0; i < 64; ++i) {
    ASSERT_EQ(index.erase(tess::Coord3{i, 1, 0}).value,
              static_cast<std::uint64_t>(i) + 1);
  }
  for (int frame = 0; frame < 4; ++frame) {
    batch.clear();
    for (std::uint64_t i = 0; i < 64; ++i) {
      batch.push(tess::EntityHandle{i + 1}, tess::PathAgentState{});
    }
  }
  EXPECT_EQ(counter.count(), 0u);
}

TEST(TessEcsAdapter, AdvanceWithIndexKeepsFieldAndIndexInSync) {
  World world;
  fill_world(world);
  tess::TileOccupancyIndex index;
  index.reserve(8);

  const tess::EntityHandle handle{7};
  tess::PathAgentBatch batch;
  batch.reserve(1);
  tess::PathAgentState agent;
  agent.position = tess::Coord3{0, 0, 0};
  install_agent(world, index, handle, agent.position);
  tess::set_path_agent_goal(agent, tess::Coord3{2, 0, 0});
  batch.push(handle, agent);

  tess::PathRequestRuntime runtime;
  reserve_runtime(runtime, 1);
  (void)tess::process_unit_path_agents<World, PassableTag>(
      world, batch.agents(), runtime);
  ASSERT_EQ(batch.agents()[0].status, tess::PathStatus::Found);

  const auto stats =
      tess::advance_path_agents_with_index<World, PassableTag, OccupancyTag,
                                           ReservationTag>(world, batch,
                                                           runtime, index, 8);
  EXPECT_EQ(stats.advanced, 2u);
  EXPECT_EQ(stats.arrived, 1u);

  // The index moved with every commit, including the arrival step.
  EXPECT_EQ(index.entity_at(tess::Coord3{2, 0, 0}), handle);
  EXPECT_EQ(index.entity_at(tess::Coord3{0, 0, 0}), tess::kNullEntityHandle);
  EXPECT_TRUE(world.template field<OccupancyTag>(tess::Coord3{2, 0, 0}));
  EXPECT_FALSE(world.template field<OccupancyTag>(tess::Coord3{0, 0, 0}));
  EXPECT_FALSE(batch.agents()[0].has_goal);
}

TEST(TessEcsAdapter, AdvanceWithIndexLeavesIndexUntouchedOnFailures) {
  World world;
  fill_world(world);
  tess::TileOccupancyIndex index;
  index.reserve(8);

  const tess::EntityHandle handle{9};
  tess::PathAgentBatch batch;
  batch.reserve(1);
  tess::PathAgentState agent;
  agent.position = tess::Coord3{0, 0, 0};
  install_agent(world, index, handle, agent.position);
  tess::set_path_agent_goal(agent, tess::Coord3{3, 0, 0});
  batch.push(handle, agent);

  tess::PathRequestRuntime runtime;
  reserve_runtime(runtime, 1);
  (void)tess::process_unit_path_agents<World, PassableTag>(
      world, batch.agents(), runtime);
  ASSERT_EQ(batch.agents()[0].status, tess::PathStatus::Found);

  // Transient failure: the next tile is occupied by a non-agent source
  // (field only -- the one-directional mirror), so the commit rejects and
  // neither the field handoff nor the index move happens.
  world.template field<OccupancyTag>(tess::Coord3{1, 0, 0}) = true;
  auto stats =
      tess::advance_path_agents_with_index<World, PassableTag, OccupancyTag,
                                           ReservationTag>(world, batch,
                                                           runtime, index, 1);
  EXPECT_EQ(stats.blocked_waits, 1u);
  EXPECT_EQ(index.entity_at(tess::Coord3{0, 0, 0}), handle);
  EXPECT_EQ(index.entity_at(tess::Coord3{1, 0, 0}), tess::kNullEntityHandle);
  EXPECT_EQ(index.size(), 1u);

  // Structural failure: the agent was teleported off its route, so the
  // non-adjacent step is rejected before any write.
  world.template field<OccupancyTag>(tess::Coord3{1, 0, 0}) = false;
  (void)tess::process_unit_path_agents<World, PassableTag>(
      world, batch.agents(), runtime);
  ASSERT_EQ(batch.agents()[0].status, tess::PathStatus::Found);
  batch.agents()[0].position = tess::Coord3{5, 5, 0};
  stats = tess::advance_path_agents_with_index<World, PassableTag, OccupancyTag,
                                               ReservationTag>(
      world, batch, runtime, index, 1);
  EXPECT_EQ(stats.movement_failures.invalid, 1u);
  EXPECT_EQ(batch.agents()[0].phase, tess::PathAgentPhase::Unreachable);
  EXPECT_EQ(index.entity_at(tess::Coord3{0, 0, 0}), handle);
  EXPECT_EQ(index.size(), 1u);
}

TEST(TessEcsAdapter, TickPipelineDrivesAgentsToArrivalWithSyncedIndex) {
  World world;
  fill_world(world);
  tess::TileOccupancyIndex index;
  index.reserve(8);
  FakeStore store;
  tess::PathAgentBatch batch;
  batch.reserve(4);

  store.spawn(0, tess::EntityHandle{100}, tess::Coord3{0, 0, 0});
  store.spawn(1, tess::EntityHandle{101}, tess::Coord3{0, 1, 0});
  store.spawn(2, tess::EntityHandle{102}, tess::Coord3{0, 2, 0});
  for (const auto& entry : store.entries) {
    install_agent(world, index, entry.handle, entry.agent.position);
  }
  store.set_goal(0, tess::Coord3{3, 0, 0});
  store.set_goal(1, tess::Coord3{3, 1, 0});
  store.set_goal(2, tess::Coord3{3, 2, 0});

  tess::PathRequestRuntime runtime;
  reserve_runtime(runtime, store.entries.size());
  tess::PathAgentTickState tick_state;
  tick_state.pathing_dirty = false;  // collect's dirty report drives it

  for (int tick = 0; tick < 3; ++tick) {
    const auto stats =
        tess::tick_ecs_unit_path_agents<World, PassableTag, OccupancyTag,
                                        ReservationTag>(
            tick_state, world, store, store, batch, runtime, index);
    // Only the first tick (armed goals) processes paths; later ticks are
    // movement-only -- the exactly-once seam.
    EXPECT_EQ(stats.processed_paths, tick == 0);
    EXPECT_EQ(stats.movement.advanced, 3u);
    for (const auto& entry : store.entries) {
      expect_agent_synced(world, index, entry.handle, entry.agent);
    }
  }

  for (const auto& entry : store.entries) {
    EXPECT_FALSE(entry.agent.has_goal);
    EXPECT_EQ(entry.agent.position.x, 3);
  }
  EXPECT_EQ(index.entity_at(tess::Coord3{3, 1, 0}), (tess::EntityHandle{101}));
  EXPECT_EQ(index.size(), 3u);
}

TEST(TessEcsAdapter, TickPipelineReprocessesOnlyOnRearmOrRepath) {
  World world;
  fill_world(world);
  tess::TileOccupancyIndex index;
  index.reserve(4);
  FakeStore store;
  tess::PathAgentBatch batch;
  batch.reserve(1);

  store.spawn(0, tess::EntityHandle{50}, tess::Coord3{0, 0, 0});
  install_agent(world, index, store.entries[0].handle,
                store.entries[0].agent.position);
  store.set_goal(0, tess::Coord3{8, 0, 0});

  tess::PathRequestRuntime runtime;
  reserve_runtime(runtime, 1);
  tess::PathAgentTickState tick_state;
  tick_state.pathing_dirty = false;

  const auto tick = [&] {
    return tess::tick_ecs_unit_path_agents<World, PassableTag, OccupancyTag,
                                           ReservationTag>(
        tick_state, world, store, store, batch, runtime, index);
  };

  auto stats = tick();
  EXPECT_TRUE(stats.processed_paths);
  EXPECT_EQ(stats.pathing.completed, 1u);

  // Quiet ticks: the ticket keeps driving movement without processing.
  stats = tick();
  EXPECT_FALSE(stats.processed_paths);
  EXPECT_EQ(stats.movement.advanced, 1u);
  stats = tick();
  EXPECT_FALSE(stats.processed_paths);

  // Re-arming a goal (even mid-flight) collects as dirty and processes
  // exactly once more.
  store.set_goal(0, tess::Coord3{0, 8, 0});
  stats = tick();
  EXPECT_TRUE(stats.processed_paths);
  stats = tick();
  EXPECT_FALSE(stats.processed_paths);
}

TEST(TessEcsAdapter, TickPipelineExcludesOffBoardEntries) {
  World world;
  fill_world(world);
  tess::TileOccupancyIndex index;
  index.reserve(4);
  FakeStore store;
  tess::PathAgentBatch batch;
  batch.reserve(2);

  store.spawn(0, tess::EntityHandle{60}, tess::Coord3{0, 0, 0});
  install_agent(world, index, store.entries[0].handle,
                store.entries[0].agent.position);
  store.set_goal(0, tess::Coord3{2, 0, 0});

  // A parked agent: no tile, no occupancy claim, no index entry. Its
  // stale goal must not path or move while parked.
  store.spawn(1, tess::EntityHandle{61}, tess::Coord3{9, 9, 0});
  tess::set_path_agent_goal(store.entries[1].agent, tess::Coord3{1, 9, 0});
  store.entries[1].off_board = true;

  tess::PathRequestRuntime runtime;
  reserve_runtime(runtime, 2);
  tess::PathAgentTickState tick_state;
  tick_state.pathing_dirty = false;

  for (int tick = 0; tick < 2; ++tick) {
    (void)tess::tick_ecs_unit_path_agents<World, PassableTag, OccupancyTag,
                                          ReservationTag>(
        tick_state, world, store, store, batch, runtime, index);
  }

  EXPECT_FALSE(store.entries[0].agent.has_goal);
  // The parked agent is untouched: still armed, never advanced.
  EXPECT_TRUE(store.entries[1].agent.has_goal);
  EXPECT_EQ(store.entries[1].agent.position, (tess::Coord3{9, 9, 0}));
  EXPECT_EQ(store.entries[1].agent.phase, tess::PathAgentPhase::NeedsPath);
  EXPECT_EQ(index.size(), 1u);
}

TEST(TessEcsAdapter, WeightedTickPairFormArrives) {
  World world;
  fill_world(world);
  tess::TileOccupancyIndex index;
  index.reserve(4);
  FakeStore store;
  tess::PathAgentBatch batch;
  batch.reserve(1);

  store.spawn(0, tess::EntityHandle{70}, tess::Coord3{0, 0, 0});
  install_agent(world, index, store.entries[0].handle,
                store.entries[0].agent.position);
  store.set_goal(0, tess::Coord3{2, 0, 0});

  tess::PathRequestRuntime runtime;
  reserve_runtime(runtime, 1);
  tess::PathAgentTickState tick_state;
  tick_state.pathing_dirty = false;

  for (int tick = 0; tick < 2; ++tick) {
    (void)tess::tick_ecs_path_agents<World, PassableTag, CostTag, 4,
                                     OccupancyTag, ReservationTag>(
        tick_state, world, store, store, batch, runtime, index);
  }

  EXPECT_FALSE(store.entries[0].agent.has_goal);
  EXPECT_EQ(store.entries[0].agent.position, (tess::Coord3{2, 0, 0}));
  EXPECT_EQ(index.entity_at(tess::Coord3{2, 0, 0}), (tess::EntityHandle{70}));
}

TEST(TessEcsAdapter, TickPipelineSteadyStateIsAllocationFree) {
  World world;
  fill_world(world);
  tess::TileOccupancyIndex index;
  index.reserve(4);
  FakeStore store;
  tess::PathAgentBatch batch;
  batch.reserve(2);

  store.spawn(0, tess::EntityHandle{80}, tess::Coord3{0, 0, 0});
  store.spawn(1, tess::EntityHandle{81}, tess::Coord3{0, 4, 0});
  for (const auto& entry : store.entries) {
    install_agent(world, index, entry.handle, entry.agent.position);
  }
  store.set_goal(0, tess::Coord3{31, 0, 0});
  store.set_goal(1, tess::Coord3{31, 4, 0});

  tess::PathRequestRuntime runtime;
  reserve_runtime(runtime, store.entries.size());
  tess::PathAgentTickState tick_state;
  tick_state.pathing_dirty = false;

  const auto tick = [&] {
    return tess::tick_ecs_unit_path_agents<World, PassableTag, OccupancyTag,
                                           ReservationTag>(
        tick_state, world, store, store, batch, runtime, index);
  };

  // Warm: the first tick processes paths, the second is a plain
  // movement tick exercising every steady-state branch once.
  (void)tick();
  (void)tick();

  tess_test::ScopedAllocationCounter counter;
  for (int i = 0; i < 16; ++i) {
    const auto stats = tick();
    ASSERT_FALSE(stats.processed_paths);
    ASSERT_EQ(stats.movement.advanced, 2u);
  }
  EXPECT_EQ(counter.count(), 0u);
}

}  // namespace
