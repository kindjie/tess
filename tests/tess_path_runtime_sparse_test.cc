#include <gtest/gtest.h>
#include <tess/tess.h>

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

TEST(TessSparsePathRuntime, MovementIntoNonResidentChunkIsRejectedNotCrash) {
  // The movement commit half of tick_*_path_agents_with_movement reads chunk
  // data through unchecked field()/meta() accessors. On a sparse world an agent
  // following a route across a chunk evicted since planning must be rejected,
  // never walked into a non-resident slot (an out-of-bounds read under NDEBUG).
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

  // `from` in the now-non-resident chunk 0.
  const auto from_missing =
      tess::validate_movement_intent<Sparse, PassableTag, OccupancyTag,
                                     ReservationTag>(
          world,
          tess::MovementIntent{tess::Coord3{0, 0, 0}, tess::Coord3{1, 0, 0}});
  EXPECT_EQ(from_missing.status, tess::MovementStatus::InvalidFrom);

  // `to` in the non-resident chunk 0, `from` in resident chunk 1.
  const auto to_missing =
      tess::validate_movement_intent<Sparse, PassableTag, OccupancyTag,
                                     ReservationTag>(
          world,
          tess::MovementIntent{tess::Coord3{32, 0, 0}, tess::Coord3{31, 0, 0}});
  EXPECT_EQ(to_missing.status, tess::MovementStatus::InvalidTo);

  // The version check helper is likewise residency-safe on a direct call.
  const auto versions = tess::movement_versions_match(
      world,
      tess::MovementIntent{tess::Coord3{0, 0, 0}, tess::Coord3{1, 0, 0}});
  EXPECT_EQ(versions, tess::MovementStatus::StaleVersion);
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
