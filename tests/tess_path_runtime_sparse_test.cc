#include <gtest/gtest.h>
#include <tess/tess.h>

#include <array>
#include <cstddef>
#include <cstdint>

// Slice 5a: the path runtime (unit route cache + weighted batch) over a
// SparseResidentWorld. The route cache's world fingerprint became residency-
// aware, so any topology edit, eviction, or reload invalidates the whole cache
// before a stale route can be served; the weighted batch fans out to the
// already-sparse weighted search/field family.
namespace {

struct PassableTag {};
struct CostTag {};
struct OccupancyTag {};
struct ReservationTag {};
using Schema = tess::FieldSchema<
    tess::Field<PassableTag, bool>, tess::Field<CostTag, std::uint32_t>,
    tess::Field<OccupancyTag, bool>, tess::Field<ReservationTag, bool>>;

// Chunks are 32 wide along x. TwoChunk/ThreeChunk give cross-chunk routes; the
// single-chunk-budget tests reuse ThreeChunk so a reload can evict + restore
// the same key at an unchanged resident count.
using ThreeChunk =
    tess::Shape<tess::Extent3{96, 32, 1}, tess::Extent3{32, 32, 1}>;
using Sparse = tess::SparseResidentWorld<ThreeChunk, Schema>;
constexpr std::size_t kTileReserve = std::size_t{96} * 32;

void fill_chunk(Sparse& world, tess::ChunkKey key) {
  world.ensure_resident(key);
  auto& page = world.chunk(key);
  for (std::uint64_t i = 0; i < Sparse::local_tile_count; ++i) {
    const auto tile = tess::LocalTileId{i};
    page.template field<PassableTag>(tile) = true;
    page.template field<CostTag>(tile) = 1u;
  }
}

tess::PathRequestRuntime make_runtime() {
  tess::PathRequestRuntime runtime;
  runtime.reserve_requests(4);
  runtime.reserve_search_nodes(kTileReserve);
  runtime.reserve_path_nodes(256);
  runtime.reserve_unit_routes(4);
  return runtime;
}

TEST(TessSparsePathRuntime, UnitCacheHitsWithinResidentSet) {
  Sparse world{tess::ResidencyConfig{3 * Sparse::page_byte_size}};
  fill_chunk(world, tess::ChunkKey{0});
  fill_chunk(world, tess::ChunkKey{1});

  auto runtime = make_runtime();
  const auto a =
      runtime.submit({tess::Coord3{0, 0, 0}, tess::Coord3{40, 0, 0}});
  const auto b =
      runtime.submit({tess::Coord3{0, 0, 0}, tess::Coord3{40, 0, 0}});
  (void)b;

  (void)runtime.process_unit_cached<Sparse, PassableTag>(world);
  EXPECT_EQ(runtime.result(a).status, tess::PathStatus::Found);
  auto stats = runtime.stats();
  EXPECT_EQ(stats.route_cache.misses, 1u);
  EXPECT_EQ(stats.route_cache.hits, 1u);
  EXPECT_EQ(stats.world_cache_invalidations, 0u);

  // Re-run with no residency or topology change: the cache is served, never
  // spuriously invalidated (the resident-set fingerprint is unchanged).
  (void)runtime.process_unit_cached<Sparse, PassableTag>(world);
  EXPECT_EQ(runtime.stats().world_cache_invalidations, 0u);
}

TEST(TessSparsePathRuntime, InPlaceEditInvalidatesCache) {
  Sparse world{tess::ResidencyConfig{3 * Sparse::page_byte_size}};
  fill_chunk(world, tess::ChunkKey{0});
  fill_chunk(world, tess::ChunkKey{1});

  auto runtime = make_runtime();
  (void)runtime.submit({tess::Coord3{0, 0, 0}, tess::Coord3{40, 0, 0}});
  (void)runtime.process_unit_cached<Sparse, PassableTag>(world);
  ASSERT_EQ(runtime.stats().world_cache_invalidations, 0u);

  // An in-place edit bumps meta().version on a still-resident chunk; the
  // fingerprint's version term catches it.
  world.field<PassableTag>(tess::Coord3{5, 0, 0}) = false;
  world.mark_dirty(tess::ChunkKey{0}, 1u,
                   tess::Box3{tess::Coord3{5, 0, 0}, tess::Extent3{1, 1, 1}});

  (void)runtime.process_unit_cached<Sparse, PassableTag>(world);
  EXPECT_EQ(runtime.stats().world_cache_invalidations, 1u);
}

TEST(TessSparsePathRuntime, EvictReloadSameKeyInvalidatesCache) {
  // Budget for exactly one chunk. The resident set is {0} before and after the
  // reload and the reloaded chunk's version resets to 0 (colliding with its
  // prior incarnation), so ONLY the strictly-increasing residency_generation
  // distinguishes them. This is the case a version-only fingerprint misses.
  Sparse world{tess::ResidencyConfig{1 * Sparse::page_byte_size}};
  fill_chunk(world, tess::ChunkKey{0});

  auto runtime = make_runtime();
  const auto a =
      runtime.submit({tess::Coord3{0, 0, 0}, tess::Coord3{31, 0, 0}});
  (void)runtime.process_unit_cached<Sparse, PassableTag>(world);
  ASSERT_EQ(runtime.result(a).status, tess::PathStatus::Found);
  ASSERT_EQ(runtime.stats().world_cache_invalidations, 0u);

  const auto gen_before = world.residency_generation(tess::ChunkKey{0});
  world.ensure_resident(tess::ChunkKey{1});  // evicts 0
  ASSERT_FALSE(world.is_resident(tess::ChunkKey{0}));
  fill_chunk(world, tess::ChunkKey{0});  // reloads 0, evicting 1
  ASSERT_EQ(world.resident_count(), 1u);
  ASSERT_GT(world.residency_generation(tess::ChunkKey{0}), gen_before);

  (void)runtime.process_unit_cached<Sparse, PassableTag>(world);
  EXPECT_EQ(runtime.stats().world_cache_invalidations, 1u);
}

TEST(TessSparsePathRuntime, EvictionOnRouteNeverServesStaleRoute) {
  // Budget for two chunks. Cache a route through chunks 0 and 1, then load a
  // third chunk so the LRU chunk 0 is evicted. The next batch must invalidate
  // and re-plan; under the default TreatAsBlocked policy the now-missing start
  // chunk reads as a wall, so the route is never served stale as Found.
  Sparse world{tess::ResidencyConfig{2 * Sparse::page_byte_size}};
  fill_chunk(world, tess::ChunkKey{0});
  fill_chunk(world, tess::ChunkKey{1});

  auto runtime = make_runtime();
  const auto a =
      runtime.submit({tess::Coord3{0, 0, 0}, tess::Coord3{40, 0, 0}});
  (void)runtime.process_unit_cached<Sparse, PassableTag>(world);
  ASSERT_EQ(runtime.result(a).status, tess::PathStatus::Found);

  fill_chunk(world, tess::ChunkKey{2});  // evicts LRU chunk 0
  ASSERT_FALSE(world.is_resident(tess::ChunkKey{0}));

  (void)runtime.process_unit_cached<Sparse, PassableTag>(world);
  EXPECT_GE(runtime.stats().world_cache_invalidations, 1u);
  EXPECT_NE(runtime.result(a).status, tess::PathStatus::Found);
}

TEST(TessSparsePathRuntime, WeightedBatchRoutesOverSparseWorld) {
  Sparse world{tess::ResidencyConfig{3 * Sparse::page_byte_size}};
  fill_chunk(world, tess::ChunkKey{0});
  fill_chunk(world, tess::ChunkKey{1});
  fill_chunk(world, tess::ChunkKey{2});

  auto runtime = make_runtime();
  const auto a =
      runtime.submit({tess::Coord3{0, 0, 0}, tess::Coord3{70, 0, 0}});
  (void)runtime.process_weighted_batch<Sparse, PassableTag, CostTag, 64>(world);
  EXPECT_EQ(runtime.result(a).status, tess::PathStatus::Found);
  // Default TreatAsBlocked policy: a fully-resident route is definite, never
  // Indeterminate.
  EXPECT_EQ(runtime.stats().indeterminate, 0u);

  // Carve a full-height wall across chunk 1: the batch re-plans over the
  // resident set and reports NoPath, never a stale Found route.
  for (std::int32_t y = 0; y < 32; ++y) {
    world.field<PassableTag>(tess::Coord3{40, y, 0}) = false;
  }
  world.mark_dirty(
      tess::ChunkKey{1}, 1u,
      tess::Box3{tess::Coord3{32, 0, 0}, tess::Extent3{32, 32, 1}});
  (void)runtime.process_weighted_batch<Sparse, PassableTag, CostTag, 64>(world);
  EXPECT_EQ(runtime.result(a).status, tess::PathStatus::NoPath);
}

// A shared-goal group member whose start sits in a NON-RESIDENT chunk must
// be excluded from the settle-target set (audit 2026-07-11 M3 arming): it
// can never settle, so arming it would hold the flood open for the whole
// resident component, and its node index has no slot in the field arrays.
// The member itself reports InvalidStart from the reader, exactly like the
// pre-M3 behavior.
TEST(TessSparsePathRuntime, NonResidentStartMemberDoesNotHoldFloodOpen) {
  Sparse world{tess::ResidencyConfig{3 * Sparse::page_byte_size}};
  fill_chunk(world, tess::ChunkKey{0});
  fill_chunk(world, tess::ChunkKey{1});
  ASSERT_FALSE(world.is_resident(tess::ChunkKey{2}));

  // Both members share the goal so the batch takes the field-build path;
  // the second start lies in never-loaded chunk 2 (x >= 64).
  const auto goal = tess::Coord3{16, 16, 0};
  const auto requests = std::array{
      tess::PathRequest{tess::Coord3{10, 16, 0}, goal},
      tess::PathRequest{tess::Coord3{70, 16, 0}, goal},
  };
  tess::WeightedPathBatchScratch scratch;
  const auto results =
      tess::weighted_path_batch<Sparse, PassableTag, CostTag, 64>(
          world, requests, scratch);
  ASSERT_EQ(results.size(), 2u);
  EXPECT_EQ(results[0].status, tess::PathStatus::Found);
  EXPECT_EQ(results[0].cost, 6u);
  EXPECT_EQ(results[1].status, tess::PathStatus::InvalidStart);
  EXPECT_EQ(scratch.stats().field_builds, 1u);
  // The resident component is 2048 tiles; settling only the distance-6
  // resident start must truncate the flood far below that.
  EXPECT_LT(results[0].reached_nodes, 200u);
}

TEST(TessSparsePathRuntime,
     MovementIntoNonResidentChunkIsTransientNotTerminal) {
  // The movement commit half of tick_*_path_agents_with_movement reads chunk
  // data through unchecked field()/meta() accessors. On a sparse world an agent
  // whose route crosses a chunk evicted since planning must be rejected with a
  // TRANSIENT status (never a non-resident-slot read, an out-of-bounds under
  // NDEBUG), so the agent lifecycle re-plans against the changed residency
  // instead of stranding at a terminal Unreachable. Ordinary LRU eviction is
  // not a permanent caller bug.
  Sparse world{tess::ResidencyConfig{1 * Sparse::page_byte_size}};
  fill_chunk(world, tess::ChunkKey{0});

  const auto ok = tess::validate_movement_intent<Sparse, PassableTag,
                                                 OccupancyTag, ReservationTag>(
      world,
      tess::MovementIntent{tess::Coord3{0, 0, 0}, tess::Coord3{1, 0, 0}});
  EXPECT_EQ(ok.status, tess::MovementStatus::Moved);

  world.ensure_resident(tess::ChunkKey{1});  // evicts chunk 0
  ASSERT_FALSE(world.is_resident(tess::ChunkKey{0}));
  ASSERT_TRUE(world.is_resident(tess::ChunkKey{1}));

  // `from` in the now-non-resident chunk 0: transient StaleVersion, not a
  // terminal Invalid* that the agent lifecycle treats as unrecoverable.
  const auto from_missing =
      tess::validate_movement_intent<Sparse, PassableTag, OccupancyTag,
                                     ReservationTag>(
          world,
          tess::MovementIntent{tess::Coord3{0, 0, 0}, tess::Coord3{1, 0, 0}});
  EXPECT_EQ(from_missing.status, tess::MovementStatus::StaleVersion);
  EXPECT_TRUE(tess::is_transient_movement_failure(from_missing.status));

  // `to` in the non-resident chunk 0, `from` in resident chunk 1.
  const auto to_missing =
      tess::validate_movement_intent<Sparse, PassableTag, OccupancyTag,
                                     ReservationTag>(
          world,
          tess::MovementIntent{tess::Coord3{32, 0, 0}, tess::Coord3{31, 0, 0}});
  EXPECT_EQ(to_missing.status, tess::MovementStatus::StaleVersion);
  EXPECT_TRUE(tess::is_transient_movement_failure(to_missing.status));

  // The version check helper reports the same transient status for the same
  // condition (the two must agree, which was the defect).
  const auto versions = tess::movement_versions_match(
      world,
      tess::MovementIntent{tess::Coord3{0, 0, 0}, tess::Coord3{1, 0, 0}});
  EXPECT_EQ(versions, tess::MovementStatus::StaleVersion);
  EXPECT_TRUE(tess::is_transient_movement_failure(versions));
}

TEST(TessSparsePathRuntime, EvictedRouteChunkReplansInsteadOfStranding) {
  // End-to-end strand repro: an agent Following a route has its own chunk
  // evicted by ordinary LRU pressure. Because the movement commit now reports a
  // TRANSIENT failure for a non-resident endpoint, the agent lifecycle parks it
  // at Blocked (re-path/retry) rather than the terminal Unreachable that would
  // permanently strand it. Budget 1 makes the eviction deterministic.
  Sparse world{tess::ResidencyConfig{1 * Sparse::page_byte_size}};
  fill_chunk(world, tess::ChunkKey{0});

  tess::PathAgentTickState tick_state;
  auto runtime = make_runtime();
  std::vector<tess::PathAgentState> agents(1);
  agents[0].position = tess::Coord3{0, 0, 0};
  world.field<OccupancyTag>(agents[0].position) = true;
  tess::set_path_agent_goal(tick_state, agents[0], tess::Coord3{20, 0, 0});

  const auto tick = [&] {
    (void)tess::tick_weighted_path_agents_with_movement<
        Sparse, PassableTag, CostTag, 64, OccupancyTag, ReservationTag>(
        tick_state, world, agents, runtime,
        tess::PathAgentTickOptions{.max_steps = 1}, 0u);
  };

  // Drive the agent until it is Following a route inside chunk 0 and has moved.
  for (int i = 0; i < 8 && agents[0].phase != tess::PathAgentPhase::Following;
       ++i) {
    tick();
  }
  ASSERT_EQ(agents[0].phase, tess::PathAgentPhase::Following);
  ASSERT_NE(agents[0].position, (tess::Coord3{0, 0, 0}));

  // Evict chunk 0 (the agent's own chunk) via budget pressure, then tick.
  world.ensure_resident(tess::ChunkKey{1});
  ASSERT_FALSE(world.is_resident(tess::ChunkKey{0}));
  tick();
  EXPECT_NE(agents[0].phase, tess::PathAgentPhase::Unreachable);
  EXPECT_EQ(agents[0].phase, tess::PathAgentPhase::Blocked);
  // The blocked step itself consumes no re-path budget; the next
  // processed tick's prepare pass counts the retry attempt.
  EXPECT_EQ(agents[0].blocked_retries, 0u);

  // Re-materialize chunk 0: the agent recovers (re-plans and resumes) instead
  // of being stranded.
  fill_chunk(world, tess::ChunkKey{0});
  world.field<OccupancyTag>(agents[0].position) = true;
  bool recovered = false;
  for (int i = 0; i < 8 && !recovered; ++i) {
    tick();
    recovered = agents[0].phase == tess::PathAgentPhase::Following ||
                agents[0].phase == tess::PathAgentPhase::Idle;
  }
  EXPECT_TRUE(recovered);
  EXPECT_NE(agents[0].phase, tess::PathAgentPhase::Unreachable);
}

TEST(TessSparsePathRuntime, RenderDeltasScanOnlyResidentChunks) {
  // render_tile_deltas / clear_render_delta_dirty must iterate the resident
  // set, not 0..chunk_count: a non-resident chunk holds no data and calling
  // meta() on it would read a non-resident slot out of bounds.
  Sparse world{tess::ResidencyConfig{2 * Sparse::page_byte_size}};
  fill_chunk(world, tess::ChunkKey{0});
  fill_chunk(world, tess::ChunkKey{1});
  ASSERT_FALSE(world.is_resident(tess::ChunkKey{2}));

  constexpr std::uint32_t mask = 1u;
  tess::clear_render_delta_dirty(world, ~0u);  // clear any setup residue
  world.mark_dirty(tess::ChunkKey{1}, mask,
                   tess::Box3{tess::Coord3{40, 0, 0}, tess::Extent3{2, 1, 1}});

  const auto deltas = tess::render_tile_deltas(world, mask);
  ASSERT_EQ(deltas.size(), 2u);
  for (const auto& d : deltas) {
    EXPECT_EQ(d.chunk_key, tess::ChunkKey{1});
  }

  tess::clear_render_delta_dirty(world, mask);
  EXPECT_TRUE(tess::render_tile_deltas(world, mask).empty());
}

}  // namespace
