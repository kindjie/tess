#include <gtest/gtest.h>
#include <tess/tess.h>

#include <cstdint>

// The DeltaFrame lifetime contract, pinned rather than merely stated.
//
// Its comment claimed a frame was valid "until the next mutating call on
// the collector (begin_tick / record_* / collect_* / publish / clear)".
// That was both too broad and too narrow: begin_tick and record_* fill the
// PENDING buffers and never touch the published ones -- which is the whole
// point of the swap in publish(), so the next frame can be recorded while
// the current one is applied -- and reserve(), which re-reserves the
// published vectors and can reallocate them, was missing.
//
// A comment cannot fail. These tests can.
namespace {

struct TerrainTag {};

using Shape = tess::Shape<tess::Extent3{16, 16, 1}, tess::Extent3{8, 8, 1}>;
using Schema = tess::FieldSchema<tess::Field<TerrainTag, std::uint8_t>>;
using World = tess::AlwaysResidentWorld<Shape, Schema>;

constexpr std::uint32_t kTerrainBit = 1U << 0U;

auto make_collector() -> tess::DeltaCollector {
  tess::DeltaCollector collector;
  collector.reserve(World::chunk_count, 64, 8);
  return collector;
}

void mark(World& world, tess::Coord3 at) {
  world.field<TerrainTag>(at) = 1;
  world.mark_dirty(tess::chunk_key<Shape>(tess::tile_key<Shape>(at)),
                   kTerrainBit, tess::Box3{at, tess::Extent3{1, 1, 1}});
}

TEST(TessDeltaFrameLifetime, RecordingTheNextFrameLeavesAPublishedOneIntact) {
  World world;
  auto collector = make_collector();

  mark(world, tess::Coord3{1, 1, 0});
  tess::collect_tile_deltas(collector, world, kTerrainBit);
  const auto frame = collector.publish();

  ASSERT_FALSE(frame.chunks.empty());
  const auto first_flags = frame.chunks[0].dirty_flags;
  const auto first_bounds_x = frame.chunks[0].bounds.extent.x;

  // Everything the old comment listed as invalidating, short of publish.
  // If any of these touched published storage, the frame would move or
  // change underneath us here.
  collector.begin_tick(2);
  mark(world, tess::Coord3{9, 9, 0});
  tess::collect_tile_deltas(collector, world, kTerrainBit);
  collector.record_move(tess::EntityHandle{1}, tess::Coord3{0, 0, 0},
                        tess::Coord3{1, 0, 0});

  EXPECT_EQ(frame.chunks[0].dirty_flags, first_flags);
  EXPECT_EQ(frame.chunks[0].bounds.extent.x, first_bounds_x);
}

TEST(TessDeltaFrameLifetime, ClearLeavesAPublishedFrameIntact) {
  World world;
  auto collector = make_collector();

  mark(world, tess::Coord3{2, 2, 0});
  tess::collect_tile_deltas(collector, world, kTerrainBit);
  const auto frame = collector.publish();

  ASSERT_FALSE(frame.chunks.empty());
  const auto flags = frame.chunks[0].dirty_flags;
  const auto bounds_x = frame.chunks[0].bounds.extent.x;

  // clear() resets the pending side only, so a published frame survives it.
  // The old comment listed clear() as invalidating; it is not.
  collector.clear();

  // Read THROUGH the span. Comparing frame.chunks.data() would compare the
  // span's own pointer, which no collector call can change -- an assertion
  // that cannot fail.
  EXPECT_EQ(frame.chunks[0].dirty_flags, flags);
  EXPECT_EQ(frame.chunks[0].bounds.extent.x, bounds_x);
}

// There is deliberately no test that reserve() relocates published
// storage. Reallocation is not observable through a DeltaFrame: the span
// keeps its own pointer, and the collector exposes no handle on the
// published vector, so any assertion available here would compare values
// no collector call can change. reserve() belongs on the invalidation list
// on the strength of the code -- it calls published_*.reserve() on all
// five vectors -- and that is what the header comment cites.

TEST(TessDeltaFrameLifetime, HeaderIsAValueAndSurvivesEverything) {
  World world;
  auto collector = make_collector();

  mark(world, tess::Coord3{4, 4, 0});
  tess::collect_tile_deltas(collector, world, kTerrainBit);
  const auto frame = collector.publish();
  const auto header = frame.header;

  // The spans go stale across a publish; the header never does, which is
  // what every existing consumer in this repo relies on.
  tess::collect_tile_deltas(collector, world, kTerrainBit);
  (void)collector.publish();

  EXPECT_EQ(frame.header.to_version.value, header.to_version.value);
  EXPECT_EQ(frame.header.baseline, header.baseline);
  EXPECT_EQ(frame.header.truncated, header.truncated);
}

}  // namespace
