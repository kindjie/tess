#include <gtest/gtest.h>
#include <tess/tess.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

#include "allocation_counter.h"

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
  s.chunk_count = frame.chunks().size();
  s.first_extent = frame.chunks()[0].bounds.extent.x;
  s.second_extent = frame.chunks()[1].bounds.extent.x;
  s.entity_count = frame.entities().size();
  s.entity_from = frame.entities()[0].from;
  s.entity_to = frame.entities()[0].to;
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
  ASSERT_GE(frame.chunks().size(), 2u);
  ASSERT_GE(frame.entities().size(), 1u);
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
  ASSERT_GE(frame.chunks().size(), 2u);
  ASSERT_GE(frame.entities().size(), 1u);
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

constexpr auto kStaleFrameDeathMessage = "stale DeltaFrame view accessed";

TEST(TessDeltaFrameDeathTest, PublishInvalidatesPreviousView) {
  tess::DeltaCollector collector;
  collector.reserve(1, 1, 1);
  const auto frame = collector.publish();
  (void)collector.publish();
  EXPECT_DEATH(static_cast<void>(frame.empty()), kStaleFrameDeathMessage);
}

TEST(TessDeltaFrameDeathTest, ReserveInvalidatesPublishedView) {
  tess::DeltaCollector collector;
  collector.reserve(1, 1, 1);
  const auto frame = collector.publish();
  collector.reserve(2, 2, 2);
  EXPECT_DEATH(static_cast<void>(frame.chunks()), kStaleFrameDeathMessage);
}

TEST(TessDeltaFrameDeathTest, MoveInvalidatesPublishedView) {
  tess::DeltaCollector source;
  source.reserve(1, 1, 1);
  const auto frame = source.publish();
  auto destination = std::move(source);
  EXPECT_DEATH(static_cast<void>(frame.entities()), kStaleFrameDeathMessage);
  static_cast<void>(destination);
}

TEST(TessDeltaFrameDeathTest,
     FailedMoveConstructionCannotLeaveADanglingLiveView) {
  if (!tess_test::allocation_failure_injection_supported()) {
    GTEST_SKIP() << "allocation failure injection is unavailable";
  }

  tess::DeltaCollector source;
  source.reserve(1, 1, 1);
  const auto frame = source.publish();
  bool threw = false;
  {
    const tess_test::ScopedAllocationFailure failure{0};
    try {
      auto destination = std::move(source);
      static_cast<void>(destination);
    } catch (const std::bad_alloc&) {
      threw = true;
    }
    EXPECT_EQ(failure.attempts(), 1u);
  }
  ASSERT_TRUE(threw);

  // The move contract chooses fail-closed invalidation before allocating:
  // validation must never succeed after transferred storage is destroyed.
  EXPECT_DEATH(static_cast<void>(frame.entities()), kStaleFrameDeathMessage);
}

TEST(TessDeltaFrameDeathTest,
     FailedMoveAssignmentInvalidatesBothPublishedViews) {
  if (!tess_test::allocation_failure_injection_supported()) {
    GTEST_SKIP() << "allocation failure injection is unavailable";
  }

  tess::DeltaCollector source;
  source.reserve(1, 1, 1);
  const auto source_frame = source.publish();
  tess::DeltaCollector destination;
  destination.reserve(1, 1, 1);
  const auto destination_frame = destination.publish();
  bool threw = false;
  {
    const tess_test::ScopedAllocationFailure failure{0};
    try {
      destination = std::move(source);
    } catch (const std::bad_alloc&) {
      threw = true;
    }
    EXPECT_EQ(failure.attempts(), 1u);
  }
  ASSERT_TRUE(threw);

  EXPECT_DEATH(static_cast<void>(source_frame.entities()),
               kStaleFrameDeathMessage);
  EXPECT_DEATH(static_cast<void>(destination_frame.entities()),
               kStaleFrameDeathMessage);
}

TEST(TessDeltaFrameDeathTest, DestructionInvalidatesPublishedView) {
  const auto frame = [] {
    tess::DeltaCollector collector;
    collector.reserve(1, 1, 1);
    return collector.publish();
  }();
  EXPECT_DEATH(static_cast<void>(frame.tiles()), kStaleFrameDeathMessage);
}

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
  static_assert(!std::is_nothrow_move_constructible_v<tess::DeltaCollector>);
  static_assert(!std::is_nothrow_move_assignable_v<tess::DeltaCollector>);

  // And the move actually works end to end, not just on paper.
  World world;
  auto collector = make_collector();
  auto moved = std::move(collector);
  mark_sized(world, tess::Coord3{1, 1, 0}, 1);
  tess::collect_tile_deltas(moved, world, kTerrainBit);
  EXPECT_FALSE(moved.publish().chunks().empty());
}

// Deleting the copy operations relocated the shared-ownership hazard
// rather than removing it. The defaulted move transfers the buffers but
// COPIES the protocol scalars, so a moved-from collector kept its version
// and its baseline flag. Review reproduced the consequence: re-reserve the
// source, let the destination collect first, and the source publishes an
// applicable, untruncated, empty frame on a chain that still looks
// continuous -- a consumer accepts it and misses a real change.
//
// A moved-from collector now behaves as if cleared.
TEST(TessDeltaFrameLifetime, MovedFromCollectorForcesAResync) {
  World world;
  auto source = make_collector();
  auto destination = std::move(source);

  // The source is moved-from. Reusing it is outside the contract, but it
  // must fail loudly rather than silently: reserve() looks like a reset.
  //
  // The use-after-move is the point of the test, so the analyzers that
  // flag it are suppressed here rather than the test being reshaped to
  // avoid them -- reshaping it would stop it exercising the hazard.
  // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
  source.reserve(World::chunk_count, 64, 8);
  mark_sized(world, tess::Coord3{1, 1, 0}, 1);

  // The destination collects first and consumes the dirty bits.
  tess::collect_tile_deltas(destination, world, kTerrainBit);

  // The source now sees nothing. Its frame must not read as an applicable
  // no-op: truncated forces the consumer to resync from a baseline.
  // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
  tess::collect_tile_deltas(source, world, kTerrainBit);
  // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
  const auto starved = source.publish();

  EXPECT_TRUE(starved.chunks().empty());
  EXPECT_TRUE(starved.header.truncated);
  EXPECT_FALSE(
      tess::delta_frame_applicable(starved.header, tess::RenderVersion{1}));
}

TEST(TessDeltaFrameLifetime, MoveDestinationIsNotPoisoned) {
  World world;
  auto source = make_collector();
  auto destination = std::move(source);

  // Only the source is poisoned; the destination is the live collector and
  // must publish ordinary applicable frames.
  mark_sized(world, tess::Coord3{2, 2, 0}, 1);
  tess::collect_tile_deltas(destination, world, kTerrainBit);
  const auto frame = destination.publish();

  EXPECT_FALSE(frame.chunks().empty());
  EXPECT_FALSE(frame.header.truncated);
}

TEST(TessDeltaFrameLifetime, AssigningAFreshCollectorClearsThePoison) {
  World world;
  auto source = make_collector();
  auto destination = std::move(source);

  // "Assign to it" is a sanctioned way to reuse a moved-from collector, so
  // the poison must not survive the assignment.
  // Assigning to a moved-from object is well defined and is the sanctioned
  // reuse path, so only the analyzer's blanket rule needs quieting.
  // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
  source = make_collector();
  mark_sized(world, tess::Coord3{3, 3, 0}, 1);
  tess::collect_tile_deltas(source, world, kTerrainBit);
  const auto frame = source.publish();

  EXPECT_FALSE(frame.chunks().empty());
  EXPECT_FALSE(frame.header.truncated);
  static_cast<void>(destination);
}

}  // namespace
