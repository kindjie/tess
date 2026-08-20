#include <gtest/gtest.h>
#include <tess/tess.h>

#include <array>
#include <cstddef>
#include <cstdint>

// Slice 5a: the path runtime (unit route cache + weighted batch) over a
// SparseResidentWorld. The route cache's world fingerprint became residency-
// aware, so any topology edit, eviction, or rematerialization invalidates the
// whole cache before a stale route can be served; the weighted batch fans out
// to the already-sparse weighted search/field family.
namespace {

struct PassableTag {};
struct CostTag {};
using WeightedMovement =
    tess::movement::MovementClass<tess::movement::Field<PassableTag>,
                                  tess::movement::FieldCost<CostTag>>;
struct OccupancyTag {};
struct ReservationTag {};
using Schema = tess::FieldSchema<
    tess::Field<PassableTag, bool>, tess::Field<CostTag, std::uint32_t>,
    tess::Field<OccupancyTag, bool>, tess::Field<ReservationTag, bool>>;

// Chunks are 32 wide along x. TwoChunk/ThreeChunk give cross-chunk routes; the
// single-chunk-budget tests reuse ThreeChunk so rematerialization can evict +
// restore the same key at an unchanged resident count.
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

  // An in-place edit bumps the content version on a still-resident chunk; the
  // fingerprint catches it.
  world.field<PassableTag>(tess::Coord3{5, 0, 0}) = false;
  world.mark_dirty(tess::ChunkKey{0}, tess::DirtyMask{1u},
                   tess::Box3{tess::Coord3{5, 0, 0}, tess::Extent3{1, 1, 1}});

  (void)runtime.process_unit_cached<Sparse, PassableTag>(world);
  EXPECT_EQ(runtime.stats().world_cache_invalidations, 1u);
}

TEST(TessSparsePathRuntime, EvictReloadSameKeyInvalidatesCache) {
  // Budget for exactly one chunk. The resident set is {0} before and after the
  // rematerialization and the chunk's content version resets to 0 (colliding
  // with its prior incarnation), so ONLY the strictly-increasing
  // residency_generation distinguishes them. This is the case a version-only
  // fingerprint misses.
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
  fill_chunk(world, tess::ChunkKey{0});  // rematerializes 0, evicting 1
  ASSERT_EQ(world.resident_count(), 1u);
  ASSERT_GT(world.residency_generation(tess::ChunkKey{0}).value,
            gen_before.value);

  (void)runtime.process_unit_cached<Sparse, PassableTag>(world);
  EXPECT_EQ(runtime.stats().world_cache_invalidations, 1u);
}

TEST(TessSparsePathRuntime, EvictionOnRouteNeverServesStaleRoute) {
  // Budget for two chunks. Cache a route through chunks 0 and 1, then load a
  // third chunk so the LRU chunk 0 is evicted. The next batch must invalidate
  // and re-plan. The now-missing start is invalid for either policy, so the
  // route is never served stale as Found.
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
  (void)runtime.process_weighted_batch<Sparse, WeightedMovement, 64>(world);
  EXPECT_EQ(runtime.result(a).status, tess::PathStatus::Found);
  // A fully resident route is definite under either missing-chunk policy.
  EXPECT_EQ(runtime.stats().indeterminate, 0u);

  // Carve a full-height wall across chunk 1: the batch re-plans over the
  // resident set and reports NoPath, never a stale Found route.
  for (std::int32_t y = 0; y < 32; ++y) {
    world.field<PassableTag>(tess::Coord3{40, y, 0}) = false;
  }
  world.mark_dirty(
      tess::ChunkKey{1}, tess::DirtyMask{1u},
      tess::Box3{tess::Coord3{32, 0, 0}, tess::Extent3{32, 32, 1}});
  (void)runtime.process_weighted_batch<Sparse, WeightedMovement, 64>(world);
  EXPECT_EQ(runtime.result(a).status, tess::PathStatus::NoPath);
}

TEST(TessSparsePathRuntime, ReplanQueuePreservesIndeterminateBoundary) {
  Sparse world{tess::ResidencyConfig{3 * Sparse::page_byte_size}};
  fill_chunk(world, tess::ChunkKey{0});
  fill_chunk(world, tess::ChunkKey{2});
  ASSERT_FALSE(world.is_resident(tess::ChunkKey{1}));

  std::array<tess::PathAgentState, 1> agents{{
      {.position = {0, 0, 0}},
  }};
  tess::set_path_agent_goal(agents[0], {64, 0, 0});
  tess::PathAgentRoutes routes;
  tess::PathAgentReplanQueue queue;
  queue.request_all(agents);
  tess::PathScratch scratch;
  scratch.reserve_nodes(kTileReserve);

  const auto stats = tess::process_unit_path_agent_replans<Sparse, PassableTag>(
      world, agents, routes, queue, scratch,
      tess::PathAgentReplanOptions{
          .max_requests = 1,
          .missing_chunk_policy = tess::MissingChunkPolicy::ReportIndeterminate,
      });

  EXPECT_EQ(stats.indeterminate, 1u);
  EXPECT_EQ(agents[0].last_result, tess::PathStatus::Indeterminate);
  EXPECT_EQ(agents[0].phase, tess::PathAgentPhase::Blocked);
  EXPECT_NE(agents[0].phase, tess::PathAgentPhase::Unreachable);
  EXPECT_TRUE(queue.empty());
}

// A shared-goal group member whose start sits in a NON-RESIDENT chunk must
// be excluded from the settle-target set:
// can never settle, so arming it would hold the flood open for the whole
// resident component, and its node index has no slot in the field arrays.
// The member itself reports InvalidStart from the reader, exactly like the
// full-field behavior.
TEST(TessSparsePathRuntime, NonResidentStartMemberDoesNotHoldFloodOpen) {
  Sparse world{tess::ResidencyConfig{3 * Sparse::page_byte_size}};
  fill_chunk(world, tess::ChunkKey{0});
  fill_chunk(world, tess::ChunkKey{1});
  ASSERT_FALSE(world.is_resident(tess::ChunkKey{2}));

  // Both members share the goal so the batch takes the field-build path;
  // the second start lies in never-materialized chunk 2 (x >= 64).
  const auto goal = tess::Coord3{16, 16, 0};
  const auto requests = std::array{
      tess::PathRequest{tess::Coord3{10, 16, 0}, goal},
      tess::PathRequest{tess::Coord3{70, 16, 0}, goal},
  };
  tess::WeightedPathBatchScratch scratch;
  const auto results = tess::weighted_path_batch<Sparse, WeightedMovement, 64>(
      world, requests, scratch, tess::MissingChunkPolicy::AssumeImpassable);
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
     SharedFieldKeepsReachedMembersDefiniteAcrossMissingBoundary) {
  Sparse world{tess::ResidencyConfig{3 * Sparse::page_byte_size}};
  fill_chunk(world, tess::ChunkKey{0});
  fill_chunk(world, tess::ChunkKey{2});
  ASSERT_FALSE(world.is_resident(tess::ChunkKey{1}));

  const auto goal = tess::Coord3{16, 16, 0};
  const auto requests = std::array{
      tess::PathRequest{tess::Coord3{10, 16, 0}, goal},
      tess::PathRequest{tess::Coord3{70, 16, 0}, goal},
  };
  tess::WeightedPathBatchScratch scratch;

  const auto report = tess::weighted_path_batch<Sparse, WeightedMovement, 64>(
      world, requests, scratch, tess::MissingChunkPolicy::ReportIndeterminate);
  ASSERT_EQ(report.size(), 2u);
  EXPECT_EQ(report[0].status, tess::PathStatus::Found);
  EXPECT_EQ(report[0].cost, 6u);
  EXPECT_EQ(report[1].status, tess::PathStatus::Indeterminate);

  const auto assume = tess::weighted_path_batch<Sparse, WeightedMovement, 64>(
      world, requests, scratch, tess::MissingChunkPolicy::AssumeImpassable);
  ASSERT_EQ(assume.size(), 2u);
  EXPECT_EQ(assume[0].status, tess::PathStatus::Found);
  EXPECT_EQ(assume[0].cost, 6u);
  EXPECT_EQ(assume[1].status, tess::PathStatus::NoPath);
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

  // `from` in the now-non-resident chunk 0: transient StaleContent, not a
  // terminal Invalid* that the agent lifecycle treats as unrecoverable.
  const auto from_missing =
      tess::validate_movement_intent<Sparse, PassableTag, OccupancyTag,
                                     ReservationTag>(
          world,
          tess::MovementIntent{tess::Coord3{0, 0, 0}, tess::Coord3{1, 0, 0}});
  EXPECT_EQ(from_missing.status, tess::MovementStatus::StaleContent);
  EXPECT_TRUE(tess::is_transient_movement_failure(from_missing.status));

  // `to` in the non-resident chunk 0, `from` in resident chunk 1.
  const auto to_missing =
      tess::validate_movement_intent<Sparse, PassableTag, OccupancyTag,
                                     ReservationTag>(
          world,
          tess::MovementIntent{tess::Coord3{32, 0, 0}, tess::Coord3{31, 0, 0}});
  EXPECT_EQ(to_missing.status, tess::MovementStatus::StaleContent);
  EXPECT_TRUE(tess::is_transient_movement_failure(to_missing.status));

  // The version check helper reports the same transient status for the same
  // condition (the two must agree, which was the defect).
  const auto versions = tess::movement_versions_match(
      world,
      tess::MovementIntent{tess::Coord3{0, 0, 0}, tess::Coord3{1, 0, 0}});
  EXPECT_EQ(versions, tess::MovementStatus::StaleContent);
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
        Sparse, WeightedMovement, 64, OccupancyTag, ReservationTag>(
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

  constexpr auto mask = tess::DirtyMask{1u};
  tess::clear_render_delta_dirty(world,
                                 ~tess::DirtyMask{});  // clear setup residue
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

// Audit finding API8: MissingChunkPolicy was unreachable from the cached
// and batched entry points, which hardcoded AssumeImpassable. Adding a route
// cache for performance therefore changed correctness semantics -- the same
// query answered NoPath cached and Indeterminate uncached. PathStatus::
// Indeterminate exists specifically so a caller never mistakes "not
// searched" for "no route exists".
TEST(TessSparsePathRuntime, CachedPathHonoursIndeterminatePolicy) {
  // Chunk 1 stays non-resident, so the route to chunk 2 must cross it.
  Sparse world{tess::ResidencyConfig{3 * Sparse::page_byte_size}};
  fill_chunk(world, tess::ChunkKey{0});
  fill_chunk(world, tess::ChunkKey{2});

  const auto request =
      tess::PathRequest{tess::Coord3{1, 1, 0}, tess::Coord3{80, 1, 0}};
  tess::PathScratch scratch;
  tess::UnitRouteCache cache;

  const auto uncached = tess::astar_path<Sparse, PassableTag>(
      world, request, scratch, tess::MissingChunkPolicy::ReportIndeterminate);
  ASSERT_EQ(uncached.status, tess::PathStatus::Indeterminate);

  const auto cached = tess::cached_astar_path<Sparse, PassableTag>(
      world, request, scratch, cache,
      tess::MissingChunkPolicy::ReportIndeterminate);
  EXPECT_EQ(cached.status, uncached.status);
}

TEST(TessSparsePathRuntime, CachedPathDefaultsToReportIndeterminate) {
  Sparse world{tess::ResidencyConfig{3 * Sparse::page_byte_size}};
  fill_chunk(world, tess::ChunkKey{0});
  fill_chunk(world, tess::ChunkKey{2});

  const auto request =
      tess::PathRequest{tess::Coord3{1, 1, 0}, tess::Coord3{80, 1, 0}};
  tess::PathScratch scratch;
  tess::UnitRouteCache cache;

  // The public default refuses to turn unknown space into a definitive
  // `NoPath` result.
  const auto cached = tess::cached_astar_path<Sparse, PassableTag>(
      world, request, scratch, cache);
  EXPECT_EQ(cached.status, tess::PathStatus::Indeterminate);
}

TEST(TessSparsePathRuntime, PublicSearchDefaultsReportIndeterminate) {
  Sparse world{tess::ResidencyConfig{3 * Sparse::page_byte_size}};
  fill_chunk(world, tess::ChunkKey{0});
  fill_chunk(world, tess::ChunkKey{2});

  const auto request =
      tess::PathRequest{tess::Coord3{1, 1, 0}, tess::Coord3{80, 1, 0}};
  tess::PathScratch path_scratch;
  EXPECT_EQ((tess::astar_path<Sparse, PassableTag>(world, request, path_scratch)
                 .status),
            tess::PathStatus::Indeterminate);
  EXPECT_EQ((tess::weighted_astar_path<Sparse, WeightedMovement>(world, request,
                                                                 path_scratch)
                 .status),
            tess::PathStatus::Indeterminate);

  tess::DistanceFieldScratch field_scratch;
  const auto goal = tess::Coord3{1, 1, 0};
  EXPECT_EQ((tess::build_distance_field<Sparse, PassableTag>(world, goal,
                                                             field_scratch)
                 .status),
            tess::PathStatus::Indeterminate);
  EXPECT_EQ((tess::build_weighted_distance_field<Sparse, WeightedMovement>(
                 world, goal, field_scratch)
                 .status),
            tess::PathStatus::Indeterminate);
  EXPECT_EQ(
      (tess::build_weighted_distance_field_in_box<Sparse, WeightedMovement>(
           world, goal,
           tess::Box3{tess::Coord3{0, 0, 0}, tess::Extent3{96, 32, 1}},
           field_scratch)
           .status),
      tess::PathStatus::Indeterminate);
  EXPECT_EQ((tess::build_bounded_weighted_distance_field<Sparse,
                                                         WeightedMovement, 64>(
                 world, goal, field_scratch)
                 .status),
            tess::PathStatus::Indeterminate);

  tess::WeightedPathBatchScratch batch_scratch;
  const auto requests = std::array{request};
  const auto batch = tess::weighted_path_batch<Sparse, WeightedMovement, 64>(
      world, requests, batch_scratch);
  ASSERT_EQ(batch.size(), 1u);
  EXPECT_EQ(batch[0].status, tess::PathStatus::Indeterminate);
}

TEST(TessSparsePathRuntime, RuntimeAndPrecheckPreserveSelectedPolicy) {
  Sparse world{tess::ResidencyConfig{3 * Sparse::page_byte_size}};
  fill_chunk(world, tess::ChunkKey{0});
  fill_chunk(world, tess::ChunkKey{2});

  tess::LocalTopologyScratch topology_scratch;
  tess::SparseRegionGraph graph;
  (void)tess::build_region_graph<Sparse, PassableTag>(world, topology_scratch,
                                                      graph);
  ASSERT_EQ(graph.local_topologies().size(), 2u);
  tess::SparseRegionGraph weighted_graph;
  (void)tess::build_region_graph<Sparse, WeightedMovement>(
      world, topology_scratch, weighted_graph);
  ASSERT_EQ(weighted_graph.local_topologies().size(), 2u);

  const auto request =
      tess::PathRequest{tess::Coord3{1, 1, 0}, tess::Coord3{80, 1, 0}};
  auto unit = make_runtime();
  const auto unit_ticket = unit.submit(request);
  (void)unit.process_unit_cached<Sparse, PassableTag>(world, {}, &graph);
  EXPECT_EQ(unit.result(unit_ticket).status, tess::PathStatus::Indeterminate);
  EXPECT_EQ(unit.stats().precheck_ruled_out, 0u);

  const auto assume_impassable = tess::PathRuntimeCachePolicy{
      .missing_chunk_policy = tess::MissingChunkPolicy::AssumeImpassable,
  };
  (void)unit.process_unit_cached<Sparse, PassableTag>(world, assume_impassable,
                                                      &graph);
  EXPECT_EQ(unit.result(unit_ticket).status, tess::PathStatus::NoPath);
  EXPECT_EQ(unit.stats().precheck_ruled_out, 1u);

  auto weighted = make_runtime();
  const auto weighted_ticket = weighted.submit(request);
  (void)weighted.process_weighted_batch<Sparse, WeightedMovement, 64>(
      world, {}, &weighted_graph);
  EXPECT_EQ(weighted.result(weighted_ticket).status,
            tess::PathStatus::Indeterminate);
  EXPECT_EQ(weighted.stats().precheck_ruled_out, 0u);

  (void)weighted.process_weighted_batch<Sparse, WeightedMovement, 64>(
      world, assume_impassable, &weighted_graph);
  EXPECT_EQ(weighted.result(weighted_ticket).status, tess::PathStatus::NoPath);
  EXPECT_EQ(weighted.stats().precheck_ruled_out, 1u);
}

TEST(TessSparsePathRuntime, AgentReplansPreserveSelectedPolicy) {
  Sparse world{tess::ResidencyConfig{3 * Sparse::page_byte_size}};
  fill_chunk(world, tess::ChunkKey{0});
  fill_chunk(world, tess::ChunkKey{2});

  const auto run = [&]<bool Weighted>(tess::MissingChunkPolicy policy) {
    std::array<tess::PathAgentState, 1> agents{{
        {.position = {1, 1, 0}},
    }};
    tess::set_path_agent_goal(agents[0], {80, 1, 0});
    tess::PathAgentRoutes routes;
    tess::PathAgentReplanQueue queue;
    queue.request_all(agents);
    tess::PathScratch scratch;
    const auto options = tess::PathAgentReplanOptions{
        .max_requests = 1,
        .missing_chunk_policy = policy,
    };
    if constexpr (Weighted) {
      (void)tess::process_weighted_path_agent_replans<Sparse, WeightedMovement>(
          world, agents, routes, queue, scratch, options);
    } else {
      (void)tess::process_unit_path_agent_replans<Sparse, PassableTag>(
          world, agents, routes, queue, scratch, options);
    }
    return agents[0].last_result;
  };

  EXPECT_EQ(run.template operator()<false>(
                tess::MissingChunkPolicy::ReportIndeterminate),
            tess::PathStatus::Indeterminate);
  EXPECT_EQ(run.template operator()<false>(
                tess::MissingChunkPolicy::AssumeImpassable),
            tess::PathStatus::NoPath);
  EXPECT_EQ(run.template operator()<true>(
                tess::MissingChunkPolicy::ReportIndeterminate),
            tess::PathStatus::Indeterminate);
  EXPECT_EQ(
      run.template operator()<true>(tess::MissingChunkPolicy::AssumeImpassable),
      tess::PathStatus::NoPath);
}

TEST(TessSparsePathRuntime, PolicyRebindDropsTheCache) {
  Sparse world{tess::ResidencyConfig{3 * Sparse::page_byte_size}};
  fill_chunk(world, tess::ChunkKey{0});
  fill_chunk(world, tess::ChunkKey{2});

  const auto request =
      tess::PathRequest{tess::Coord3{1, 1, 0}, tess::Coord3{80, 1, 0}};
  tess::PathScratch scratch;
  tess::UnitRouteCache cache;

  // An entry stored under one policy must never be served to a caller who
  // asked for the other: the two disagree precisely on this status.
  const auto blocked = tess::cached_astar_path<Sparse, PassableTag>(
      world, request, scratch, cache,
      tess::MissingChunkPolicy::AssumeImpassable);
  ASSERT_EQ(blocked.status, tess::PathStatus::NoPath);
  ASSERT_EQ(cache.stats().policy_rebinds, 0u);

  const auto repeated = tess::cached_astar_path<Sparse, PassableTag>(
      world, request, scratch, cache,
      tess::MissingChunkPolicy::AssumeImpassable);
  EXPECT_EQ(repeated.status, tess::PathStatus::NoPath);
  EXPECT_EQ(cache.stats().hits, 1u);

  const auto indeterminate = tess::cached_astar_path<Sparse, PassableTag>(
      world, request, scratch, cache,
      tess::MissingChunkPolicy::ReportIndeterminate);
  EXPECT_EQ(indeterminate.status, tess::PathStatus::Indeterminate);
  EXPECT_EQ(cache.stats().policy_rebinds, 1u);
}

}  // namespace
