#include <gtest/gtest.h>
#include <tess/tess.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

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

// Every published record carries DISTINCT values. A first draft marked two
// chunks identically, so swapping or shifting the published records would
// have passed; and it observed only `chunks`, so corruption of
// `published_entities_` by record_move would have gone unnoticed.
void mark_sized(World& world, tess::Coord3 at,
                decltype(tess::Extent3{}.x) extent) {
  world.field<TerrainTag>(at) = 1;
  world.mark_dirty(tess::chunk_key<Shape>(tess::tile_key<Shape>(at)),
                   kTerrainBit,
                   tess::Box3{at, tess::Extent3{extent, extent, 1}});
}

struct PublishedSnapshot {
  std::size_t chunk_count = 0;
  // Deliberately spelled from the source type rather than a guessed width:
  // Extent3's members are 64-bit, and capturing them as uint32_t is a
  // narrowing conversion GCC rejects under -Werror=conversion.
  decltype(tess::Extent3{}.x) first_extent = 0;
  decltype(tess::Extent3{}.x) second_extent = 0;
  std::size_t entity_count = 0;
  tess::Coord3 entity_from{};
  tess::Coord3 entity_to{};
};

[[nodiscard]] auto snapshot(const tess::DeltaFrame& frame)
    -> PublishedSnapshot {
  PublishedSnapshot s;
  s.chunk_count = frame.chunks.size();
  s.first_extent = frame.chunks[0].bounds.extent.x;
  s.second_extent = frame.chunks[1].bounds.extent.x;
  s.entity_count = frame.entities.size();
  s.entity_from = frame.entities[0].from;
  s.entity_to = frame.entities[0].to;
  return s;
}

[[nodiscard]] auto publish_two_distinct_chunks(World& world,
                                               tess::DeltaCollector& collector)
    -> tess::DeltaFrame {
  collector.begin_tick(1);
  mark_sized(world, tess::Coord3{1, 1, 0}, 1);
  mark_sized(world, tess::Coord3{9, 9, 0}, 4);
  collector.record_move(tess::EntityHandle{11}, tess::Coord3{0, 0, 0},
                        tess::Coord3{1, 0, 0});
  tess::collect_tile_deltas(collector, world, kTerrainBit);
  return collector.publish();
}

TEST(TessDeltaFrameLifetime, RecordingTheNextFrameLeavesAPublishedOneIntact) {
  World world;
  auto collector = make_collector();

  const auto frame = publish_two_distinct_chunks(world, collector);
  ASSERT_GE(frame.chunks.size(), 2u);
  ASSERT_GE(frame.entities.size(), 1u);
  const auto before = snapshot(frame);
  ASSERT_NE(before.first_extent, before.second_extent);

  // Everything the old comment listed as invalidating, short of publish.
  // These fill the PENDING buffers; if any touched published storage the
  // snapshot below would differ.
  collector.begin_tick(2);
  mark_sized(world, tess::Coord3{2, 2, 0}, 8);
  tess::collect_tile_deltas(collector, world, kTerrainBit);
  collector.record_move(tess::EntityHandle{22}, tess::Coord3{5, 5, 0},
                        tess::Coord3{6, 5, 0});

  const auto after = snapshot(frame);
  EXPECT_EQ(after.chunk_count, before.chunk_count);
  EXPECT_EQ(after.first_extent, before.first_extent);
  EXPECT_EQ(after.second_extent, before.second_extent);
  EXPECT_EQ(after.entity_count, before.entity_count);
  EXPECT_EQ(after.entity_from, before.entity_from);
  EXPECT_EQ(after.entity_to, before.entity_to);
}

TEST(TessDeltaFrameLifetime, ClearLeavesAPublishedFrameIntact) {
  World world;
  auto collector = make_collector();

  const auto frame = publish_two_distinct_chunks(world, collector);
  ASSERT_GE(frame.chunks.size(), 2u);
  ASSERT_GE(frame.entities.size(), 1u);
  const auto before = snapshot(frame);

  // clear() resets the pending side only, so a published frame survives it.
  // The old comment listed clear() as invalidating; it is not.
  collector.clear();

  const auto after = snapshot(frame);
  EXPECT_EQ(after.chunk_count, before.chunk_count);
  EXPECT_EQ(after.first_extent, before.first_extent);
  EXPECT_EQ(after.second_extent, before.second_extent);
  EXPECT_EQ(after.entity_count, before.entity_count);
  EXPECT_EQ(after.entity_from, before.entity_from);
}

// What these tests can and cannot catch, stated so the next reader does not
// over-trust them: they detect published records being REPLACED or shifted.
// They cannot detect a bare `published_chunks_.clear()`, because clearing a
// vector of trivial elements leaves the bytes in place, so reads through
// the span keep returning the old values -- undefined behaviour that
// nonetheless produces the expected numbers. No assertion available from
// outside the collector distinguishes that case.

// There is deliberately no test that reserve() relocates published
// storage. Reallocation is not observable through a DeltaFrame: the span
// keeps its own pointer, and the collector exposes no handle on the
// published vector, so any assertion available here would compare values
// no collector call can change. reserve() belongs on the invalidation list
// on the strength of the code -- it calls published_*.reserve() on all
// five vectors -- and that is what the header comment cites.

// There is no test that `header` survives a publish. It is a value member
// of the caller's own DeltaFrame, so an assertion comparing it against a
// copy of itself cannot fail -- the first draft here contained exactly
// that. Its durability is a property of the type's layout, which
// tess_render_delta_frame_test already exercises throughout.

// A copy would duplicate all five buffer pairs silently and leave two
// collectors each believing they alone clear the dirty bits they collect.
// Collection consumes those bits, so the second collector over the same
// world observes nothing and publishes an empty frame that still advances
// its own version -- a consumer on that chain silently misses everything.
TEST(TessDeltaFrameLifetime, CollectorIsMoveOnly) {
  static_assert(!std::is_copy_constructible_v<tess::DeltaCollector>);
  static_assert(!std::is_copy_assignable_v<tess::DeltaCollector>);

  // Movable on purpose: factories return one by value. Declaring the copy
  // operations as deleted suppresses the implicit moves, so this fails if
  // they are ever dropped rather than defaulted.
  static_assert(std::is_move_constructible_v<tess::DeltaCollector>);
  static_assert(std::is_move_assignable_v<tess::DeltaCollector>);

  // And the move actually works end to end, not just on paper.
  World world;
  auto collector = make_collector();
  auto moved = std::move(collector);
  mark_sized(world, tess::Coord3{1, 1, 0}, 1);
  tess::collect_tile_deltas(moved, world, kTerrainBit);
  EXPECT_FALSE(moved.publish().chunks.empty());
}

}  // namespace
