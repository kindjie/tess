#include <gtest/gtest.h>
#include <tess/tess.h>

#include <cstddef>
#include <cstdint>
#include <set>
#include <utility>

#include "allocation_counter.h"

namespace {

struct TerrainTag {};
struct DecorTag {};

using Shape = tess::Shape<tess::Extent3{32, 32, 1}, tess::Extent3{8, 8, 1}>;
using Schema = tess::FieldSchema<tess::Field<TerrainTag, std::uint8_t>,
                                 tess::Field<DecorTag, std::uint8_t>>;
using World = tess::AlwaysResidentWorld<Shape, Schema>;

constexpr std::uint32_t kTerrainBit = 1U << 0U;
constexpr std::uint32_t kDecorBit = 1U << 1U;

void mark_box(World& world, tess::Coord3 origin, tess::Extent3 extent,
              std::uint32_t flags) {
  world.mark_dirty(tess::chunk_key<Shape>(tess::tile_key<Shape>(origin)), flags,
                   tess::Box3{origin, extent});
}

auto make_collector(std::uint32_t threshold = 64, bool coalesce = true)
    -> tess::DeltaCollector {
  tess::DeltaCollector collector(
      tess::DeltaCollectorOptions{threshold, coalesce});
  collector.reserve(64, 4096, 32);
  return collector;
}

auto frame_tile_set(const tess::DeltaFrame& frame)
    -> std::set<std::pair<std::int64_t, std::int64_t>> {
  std::set<std::pair<std::int64_t, std::int64_t>> tiles;
  for (const auto& chunk : frame.chunks) {
    if (chunk.tile_count != 0) {
      for (std::uint32_t i = 0; i < chunk.tile_count; ++i) {
        const auto& tile = frame.tiles[chunk.first_tile + i];
        tiles.emplace(tile.coord.x, tile.coord.y);
      }
    } else {
      const auto end_x = chunk.bounds.origin.x +
                         static_cast<std::int64_t>(chunk.bounds.extent.x);
      const auto end_y = chunk.bounds.origin.y +
                         static_cast<std::int64_t>(chunk.bounds.extent.y);
      for (auto y = chunk.bounds.origin.y; y < end_y; ++y) {
        for (auto x = chunk.bounds.origin.x; x < end_x; ++x) {
          tiles.emplace(x, y);
        }
      }
    }
  }
  return tiles;
}

TEST(TessDeltaFrame, EmptyPublishDoesNotBumpTheVersion) {
  auto collector = make_collector();
  EXPECT_EQ(collector.version().value, 1u);

  const auto frame = collector.publish();
  EXPECT_TRUE(frame.empty());
  EXPECT_EQ(frame.header.from_version.value, 1u);
  EXPECT_EQ(frame.header.to_version.value, 1u);
  EXPECT_FALSE(frame.header.baseline);
  EXPECT_FALSE(frame.header.truncated);
  EXPECT_EQ(collector.version().value, 1u);
}

TEST(TessDeltaFrame, StateCarryingPublishBumpsTheVersionByOne) {
  World world;
  auto collector = make_collector();
  mark_box(world, tess::Coord3{2, 3, 0}, tess::Extent3{2, 2, 1}, kTerrainBit);
  tess::collect_tile_deltas(collector, world, kTerrainBit);

  const auto frame = collector.publish();
  EXPECT_FALSE(frame.empty());
  EXPECT_EQ(frame.header.from_version.value, 1u);
  EXPECT_EQ(frame.header.to_version.value, 2u);
  EXPECT_EQ(frame.header.dirty_mask, kTerrainBit);
  EXPECT_EQ(collector.version().value, 2u);
}

TEST(TessDeltaFrame, ApplicabilityTruthTable) {
  auto header = tess::DeltaFrameHeader{};
  header.from_version = tess::RenderVersion{3};
  header.to_version = tess::RenderVersion{4};

  // Exact chain match applies; anything else does not.
  EXPECT_TRUE(tess::delta_frame_applicable(header, tess::RenderVersion{3}));
  EXPECT_FALSE(tess::delta_frame_applicable(header, tess::RenderVersion{2}));
  EXPECT_FALSE(tess::delta_frame_applicable(header, tess::RenderVersion{4}));

  // A fresh consumer ({0}) never applies a non-baseline frame, even
  // against a hypothetical from_version 0 (collectors start at 1).
  auto fresh = header;
  fresh.from_version = tess::RenderVersion{0};
  EXPECT_FALSE(tess::delta_frame_applicable(fresh, tess::RenderVersion{0}));

  // Baselines always apply, including to fresh consumers.
  auto baseline = header;
  baseline.baseline = true;
  EXPECT_TRUE(tess::delta_frame_applicable(baseline, tess::RenderVersion{0}));
  EXPECT_TRUE(tess::delta_frame_applicable(baseline, tess::RenderVersion{99}));

  // Truncation is a structural gap -- even for baselines: a baseline
  // that overflowed chunk storage covers only part of the world.
  auto truncated = header;
  truncated.truncated = true;
  EXPECT_FALSE(tess::delta_frame_applicable(truncated, tess::RenderVersion{3}));
  truncated.baseline = true;
  EXPECT_FALSE(tess::delta_frame_applicable(truncated, tess::RenderVersion{3}));
}

TEST(TessDeltaFrame, SmallDirtyBoxesEmitPerTileRecordsMatchingLegacy) {
  World world;
  auto collector = make_collector();
  mark_box(world, tess::Coord3{1, 1, 0}, tess::Extent3{3, 2, 1}, kTerrainBit);
  mark_box(world, tess::Coord3{9, 9, 0}, tess::Extent3{1, 4, 1}, kTerrainBit);

  // Legacy comparison set captured BEFORE the new collector clears the
  // dirty metadata.
  const auto legacy = tess::render_tile_deltas(world, kTerrainBit);
  std::set<std::pair<std::int64_t, std::int64_t>> legacy_tiles;
  for (const auto& delta : legacy) {
    legacy_tiles.emplace(delta.coord.x, delta.coord.y);
  }

  tess::collect_tile_deltas(collector, world, kTerrainBit);
  const auto frame = collector.publish();

  EXPECT_EQ(frame.chunks.size(), 2u);
  for (const auto& chunk : frame.chunks) {
    EXPECT_NE(chunk.tile_count, 0u);
    EXPECT_EQ(chunk.dirty_flags, kTerrainBit);
  }
  EXPECT_EQ(frame_tile_set(frame), legacy_tiles);
}

TEST(TessDeltaFrame, LargeDirtyBoxesEmitOneClippedBoxRecord) {
  World world;
  auto collector = make_collector(/*threshold=*/16);
  // A dirty box spanning a whole 8x8 chunk (64 tiles > threshold 16).
  mark_box(world, tess::Coord3{8, 8, 0}, tess::Extent3{8, 8, 1}, kTerrainBit);

  tess::collect_tile_deltas(collector, world, kTerrainBit);
  const auto frame = collector.publish();

  ASSERT_EQ(frame.chunks.size(), 1u);
  EXPECT_EQ(frame.chunks[0].tile_count, 0u);
  EXPECT_EQ(frame.chunks[0].bounds.origin, (tess::Coord3{8, 8, 0}));
  EXPECT_EQ(frame.chunks[0].bounds.extent.x, 8u);
  EXPECT_EQ(frame.chunks[0].bounds.extent.y, 8u);
  EXPECT_TRUE(frame.tiles.empty());
}

TEST(TessDeltaFrame, ThresholdZeroIsAlwaysBoxGranular) {
  World world;
  auto collector = make_collector(/*threshold=*/0);
  mark_box(world, tess::Coord3{4, 4, 0}, tess::Extent3{1, 1, 1}, kTerrainBit);

  tess::collect_tile_deltas(collector, world, kTerrainBit);
  const auto frame = collector.publish();

  ASSERT_EQ(frame.chunks.size(), 1u);
  EXPECT_EQ(frame.chunks[0].tile_count, 0u);
  EXPECT_EQ(frame.chunks[0].bounds.extent.x, 1u);
  EXPECT_TRUE(frame.tiles.empty());
}

TEST(TessDeltaFrame, CollectionClearsOnlyTheCollectedMask) {
  World world;
  auto collector = make_collector();
  const auto key =
      tess::chunk_key<Shape>(tess::tile_key<Shape>(tess::Coord3{2, 2, 0}));
  mark_box(world, tess::Coord3{2, 2, 0}, tess::Extent3{2, 2, 1},
           kTerrainBit | kDecorBit);

  tess::collect_tile_deltas(collector, world, kTerrainBit);
  (void)collector.publish();

  // The terrain bit is consumed; the decor bit (another owner) remains.
  EXPECT_EQ(world.observe_dirty(key, kTerrainBit).flags, 0u);
  EXPECT_EQ(world.observe_dirty(key, kDecorBit).flags, kDecorBit);

  // Re-collecting under the mask emits nothing new.
  tess::collect_tile_deltas(collector, world, kTerrainBit);
  const auto quiet = collector.publish();
  EXPECT_TRUE(quiet.empty());
}

TEST(TessDeltaFrame, RacingMarkBetweenObserveAndClearIsNeverLost) {
  World world;
  const auto key =
      tess::chunk_key<Shape>(tess::tile_key<Shape>(tess::Coord3{0, 0, 0}));
  mark_box(world, tess::Coord3{0, 0, 0}, tess::Extent3{1, 1, 1}, kTerrainBit);

  const auto observed = world.observe_dirty(key, kTerrainBit);
  // A mark lands between observe and clear: the observed-generation
  // clear refuses, leaving the union for the next collection.
  mark_box(world, tess::Coord3{3, 3, 0}, tess::Extent3{1, 1, 1}, kTerrainBit);
  EXPECT_FALSE(world.clear_dirty_observed(key, observed));
  EXPECT_EQ(world.observe_dirty(key, kTerrainBit).flags, kTerrainBit);
}

TEST(TessDeltaFrame, MovesCoalesceLastWriterWinsWithinAFrame) {
  auto collector = make_collector();
  const auto pawn = tess::EntityHandle{7};
  collector.begin_tick(10);
  collector.record_move(pawn, tess::Coord3{0, 0, 0}, tess::Coord3{1, 0, 0});
  collector.begin_tick(11);
  collector.record_move(pawn, tess::Coord3{1, 0, 0}, tess::Coord3{2, 0, 0});
  collector.begin_tick(12);
  collector.record_move(pawn, tess::Coord3{2, 0, 0}, tess::Coord3{2, 1, 0});

  const auto frame = collector.publish();
  ASSERT_EQ(frame.entities.size(), 1u);
  EXPECT_EQ(frame.entities[0].kind, tess::EntityDeltaKind::Moved);
  EXPECT_EQ(frame.entities[0].from, (tess::Coord3{0, 0, 0}));
  EXPECT_EQ(frame.entities[0].to, (tess::Coord3{2, 1, 0}));
  EXPECT_EQ(frame.entities[0].last_tick, 12u);
  EXPECT_EQ(frame.header.first_tick, 10u);
  EXPECT_EQ(frame.header.last_tick, 12u);
  EXPECT_EQ(frame.header.ticks, 3u);
  EXPECT_EQ(collector.stats().moves_coalesced, 2u);

  // The coalescing window closes at publish: the next frame's move is a
  // fresh record, never folded into published state.
  collector.begin_tick(13);
  collector.record_move(pawn, tess::Coord3{2, 1, 0}, tess::Coord3{3, 1, 0});
  const auto next = collector.publish();
  ASSERT_EQ(next.entities.size(), 1u);
  EXPECT_EQ(next.entities[0].from, (tess::Coord3{2, 1, 0}));
}

TEST(TessDeltaFrame, BarriersSplitCoalescing) {
  auto collector = make_collector();
  const auto pawn = tess::EntityHandle{9};
  collector.begin_tick(1);
  collector.record_move(pawn, tess::Coord3{0, 0, 0}, tess::Coord3{1, 0, 0});
  collector.record_teleport(pawn, tess::Coord3{1, 0, 0}, tess::Coord3{8, 8, 0});
  collector.record_move(pawn, tess::Coord3{8, 8, 0}, tess::Coord3{8, 9, 0});
  collector.record_move(pawn, tess::Coord3{8, 9, 0}, tess::Coord3{8, 10, 0});

  const auto frame = collector.publish();
  ASSERT_EQ(frame.entities.size(), 3u);
  EXPECT_EQ(frame.entities[0].kind, tess::EntityDeltaKind::Moved);
  EXPECT_EQ(frame.entities[1].kind, tess::EntityDeltaKind::Teleported);
  EXPECT_EQ(frame.entities[2].kind, tess::EntityDeltaKind::Moved);
  EXPECT_EQ(frame.entities[2].from, (tess::Coord3{8, 8, 0}));
  EXPECT_EQ(frame.entities[2].to, (tess::Coord3{8, 10, 0}));
}

TEST(TessDeltaFrame, CoalescingDisabledKeepsEveryStep) {
  auto collector = make_collector(64, /*coalesce=*/false);
  const auto pawn = tess::EntityHandle{3};
  collector.begin_tick(1);
  collector.record_move(pawn, tess::Coord3{0, 0, 0}, tess::Coord3{1, 0, 0});
  collector.begin_tick(2);
  collector.record_move(pawn, tess::Coord3{1, 0, 0}, tess::Coord3{2, 0, 0});

  const auto frame = collector.publish();
  ASSERT_EQ(frame.entities.size(), 2u);
  EXPECT_EQ(frame.entities[0].to, (tess::Coord3{1, 0, 0}));
  EXPECT_EQ(frame.entities[1].from, (tess::Coord3{1, 0, 0}));
  EXPECT_EQ(collector.stats().moves_coalesced, 0u);
}

TEST(TessDeltaFrame, EntityCapacityOverflowTruncatesTheFrame) {
  tess::DeltaCollector collector;
  collector.reserve(4, 16, 2);
  collector.begin_tick(1);
  collector.record_spawn(tess::EntityHandle{1}, tess::Coord3{0, 0, 0});
  collector.record_spawn(tess::EntityHandle{2}, tess::Coord3{1, 0, 0});
  collector.record_spawn(tess::EntityHandle{3}, tess::Coord3{2, 0, 0});

  const auto frame = collector.publish();
  EXPECT_EQ(frame.entities.size(), 2u);
  EXPECT_TRUE(frame.header.truncated);
  EXPECT_FALSE(
      tess::delta_frame_applicable(frame.header, frame.header.from_version));
  EXPECT_EQ(collector.stats().truncations, 1u);

  // The next in-capacity frame chains normally again.
  collector.begin_tick(2);
  collector.record_spawn(tess::EntityHandle{4}, tess::Coord3{3, 0, 0});
  const auto next = collector.publish();
  EXPECT_FALSE(next.header.truncated);
}

TEST(TessDeltaFrame, TileOverflowDegradesToABoxRecordWithoutTruncation) {
  World world;
  tess::DeltaCollector collector(tess::DeltaCollectorOptions{64, true});
  collector.reserve(8, 4, 4);  // room for only 4 tile records
  mark_box(world, tess::Coord3{0, 0, 0}, tess::Extent3{3, 3, 1}, kTerrainBit);

  tess::collect_tile_deltas(collector, world, kTerrainBit);
  const auto frame = collector.publish();

  ASSERT_EQ(frame.chunks.size(), 1u);
  EXPECT_EQ(frame.chunks[0].tile_count, 0u);  // box fallback
  EXPECT_EQ(frame.chunks[0].bounds.extent.x, 3u);
  EXPECT_FALSE(frame.header.truncated);
}

TEST(TessDeltaFrame, ClearPoisonsTheStreamUntilABaseline) {
  World world;
  auto collector = make_collector();

  // Records lost to a hard reset are unrecoverable: the stream is
  // poisoned and the next delta publish must not be applied.
  collector.begin_tick(1);
  collector.record_spawn(tess::EntityHandle{1}, tess::Coord3{0, 0, 0});
  collector.clear();

  mark_box(world, tess::Coord3{5, 5, 0}, tess::Extent3{1, 1, 1}, kTerrainBit);
  tess::collect_tile_deltas(collector, world, kTerrainBit);
  const auto poisoned = collector.publish();
  EXPECT_TRUE(poisoned.header.truncated);
  EXPECT_FALSE(tess::delta_frame_applicable(poisoned.header,
                                            poisoned.header.from_version));

  // A baseline heals the stream (full baseline collection lands in the
  // S9.3 slice; the pending flag is the seam under test here).
  collector.mark_baseline_pending();
  const auto baseline = collector.publish();
  EXPECT_TRUE(baseline.header.baseline);
  EXPECT_FALSE(baseline.header.truncated);

  mark_box(world, tess::Coord3{6, 6, 0}, tess::Extent3{1, 1, 1}, kTerrainBit);
  tess::collect_tile_deltas(collector, world, kTerrainBit);
  const auto healed = collector.publish();
  EXPECT_FALSE(healed.header.truncated);
}

TEST(TessDeltaFrame, BaselinePublishDropsPendingEntityRecords) {
  auto collector = make_collector();
  collector.begin_tick(1);
  collector.record_spawn(tess::EntityHandle{1}, tess::Coord3{0, 0, 0});
  collector.record_move(tess::EntityHandle{1}, tess::Coord3{0, 0, 0},
                        tess::Coord3{1, 0, 0});
  collector.mark_baseline_pending();

  // The consumer re-snapshots its entity presentation on every baseline
  // apply, so carrying entity records would double-apply them.
  const auto frame = collector.publish();
  EXPECT_TRUE(frame.header.baseline);
  EXPECT_TRUE(frame.entities.empty());
}

TEST(TessDeltaFrame, FullBaselineCoversEveryChunkAndHealsTheStream) {
  World world;
  tess::DeltaCollector collector;
  collector.reserve(World::chunk_count, 256, 8);

  // Pending dirty records are superseded by the baseline; the dirty
  // bits under the mask are consumed.
  mark_box(world, tess::Coord3{3, 3, 0}, tess::Extent3{2, 2, 1}, kTerrainBit);
  tess::collect_tile_deltas(collector, world, kTerrainBit);
  mark_box(world, tess::Coord3{20, 20, 0}, tess::Extent3{1, 1, 1}, kTerrainBit);
  tess::collect_baseline(collector, world, kTerrainBit);

  const auto frame = collector.publish();
  EXPECT_TRUE(frame.header.baseline);
  EXPECT_FALSE(frame.header.truncated);
  ASSERT_EQ(frame.chunks.size(), World::chunk_count);
  for (const auto& chunk : frame.chunks) {
    EXPECT_EQ(chunk.tile_count, 0u);
    EXPECT_EQ(chunk.dirty_flags, kTerrainBit);
    EXPECT_EQ(chunk.bounds.extent.x, 8u);
    EXPECT_EQ(chunk.bounds.extent.y, 8u);
  }
  // A fresh consumer can adopt it.
  EXPECT_TRUE(
      tess::delta_frame_applicable(frame.header, tess::RenderVersion{0}));

  // Everything under the mask was consumed.
  tess::collect_tile_deltas(collector, world, kTerrainBit);
  EXPECT_TRUE(collector.publish().empty());
}

TEST(TessDeltaFrame, OverflowingBaselineIsTruncatedAndNeverApplicable) {
  World world;
  tess::DeltaCollector collector;
  collector.reserve(4, 16, 4);  // world has 16 chunks; only 4 fit

  tess::collect_baseline(collector, world, kTerrainBit);
  const auto frame = collector.publish();
  EXPECT_TRUE(frame.header.baseline);
  EXPECT_TRUE(frame.header.truncated);
  EXPECT_FALSE(
      tess::delta_frame_applicable(frame.header, tess::RenderVersion{0}));
}

TEST(TessDeltaFrame, OverlaysStageCopiesAndReplaceEveryFrame) {
  tess::DeltaCollector collector;
  collector.reserve(4, 16, 4, 4, 32);

  std::vector<tess::Coord3> route{{0, 0, 0}, {1, 0, 0}, {2, 0, 0}};
  {
    const auto view = tess::PathView{route};
    collector.stage_path_overlay(tess::EntityHandle{5}, tess::PathTicket{},
                                 view.suffix(1));
  }
  // Mutate the source after staging: the frame must carry the copy.
  route.assign({{9, 9, 0}, {9, 9, 0}, {9, 9, 0}});

  const auto frame = collector.publish();
  ASSERT_EQ(frame.overlays.size(), 1u);
  EXPECT_EQ(frame.overlays[0].entity, (tess::EntityHandle{5}));
  ASSERT_EQ(frame.overlays[0].node_count, 2u);
  EXPECT_EQ(frame.overlay_nodes[frame.overlays[0].first_node],
            (tess::Coord3{1, 0, 0}));
  EXPECT_EQ(frame.overlay_nodes[frame.overlays[0].first_node + 1],
            (tess::Coord3{2, 0, 0}));

  // Overlays never affect versioning or emptiness: full replacement,
  // and the next frame without staging carries none.
  EXPECT_TRUE(frame.empty());
  EXPECT_EQ(frame.header.from_version, frame.header.to_version);
  const auto next = collector.publish();
  EXPECT_TRUE(next.overlays.empty());
}

TEST(TessDeltaFrame, OverlayOverflowDropsTheOverlayNotTheFrame) {
  tess::DeltaCollector collector;
  collector.reserve(4, 16, 4, 1, 4);

  std::vector<tess::Coord3> long_route(8, tess::Coord3{1, 1, 0});
  const auto view = tess::PathView{long_route};
  collector.stage_path_overlay(tess::EntityHandle{1}, tess::PathTicket{}, view);
  EXPECT_EQ(collector.stats().overlay_truncations, 1u);

  std::vector<tess::Coord3> short_route(3, tess::Coord3{2, 2, 0});
  const auto short_view = tess::PathView{short_route};
  collector.stage_path_overlay(tess::EntityHandle{2}, tess::PathTicket{},
                               short_view);

  const auto frame = collector.publish();
  ASSERT_EQ(frame.overlays.size(), 1u);
  EXPECT_EQ(frame.overlays[0].entity, (tess::EntityHandle{2}));
  EXPECT_FALSE(frame.header.truncated);
}

TEST(TessDeltaFrame, SteadyStateCollectionIsAllocationFree) {
  World world;
  auto collector = make_collector();

  // Warm one full cycle.
  mark_box(world, tess::Coord3{1, 1, 0}, tess::Extent3{2, 2, 1}, kTerrainBit);
  collector.begin_tick(1);
  collector.record_move(tess::EntityHandle{1}, tess::Coord3{0, 0, 0},
                        tess::Coord3{1, 0, 0});
  tess::collect_tile_deltas(collector, world, kTerrainBit);
  (void)collector.publish();

  tess_test::ScopedAllocationCounter counter;
  for (std::uint64_t tick = 2; tick < 34; ++tick) {
    mark_box(world, tess::Coord3{1, 1, 0}, tess::Extent3{2, 2, 1}, kTerrainBit);
    collector.begin_tick(tick);
    collector.record_move(tess::EntityHandle{1}, tess::Coord3{0, 0, 0},
                          tess::Coord3{1, 0, 0});
    tess::collect_tile_deltas(collector, world, kTerrainBit);
    const auto frame = collector.publish();
    ASSERT_FALSE(frame.empty());
  }
  EXPECT_EQ(counter.count(), 0u);
}

}  // namespace
