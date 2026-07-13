#include <gtest/gtest.h>
#include <tess/core/assert.h>
#include <tess/tess.h>

#include <cstdint>
#include <utility>

namespace {

struct TerrainTag {};

using TopDown2D =
    tess::Shape<tess::Extent3{128, 64, 1}, tess::Extent3{32, 16, 1}>;

using TerrainField = tess::Field<TerrainTag, std::uint16_t>;
using Schema = tess::FieldSchema<TerrainField>;
using World = tess::AlwaysResidentWorld<TopDown2D, Schema>;

#if TESS_ENABLE_ASSERTS

struct PassableTag {};

using PathSchema = tess::FieldSchema<tess::Field<PassableTag, bool>>;
using PathWorld = tess::AlwaysResidentWorld<TopDown2D, PathSchema>;

void fill_passable(PathWorld& world) {
  for (auto& page : world.chunks()) {
    for (auto& tile : page.template field_span<PassableTag>()) {
      tile = true;
    }
  }
}

constexpr auto kAssertDeathMessage = "tess assertion failed";

#endif  // TESS_ENABLE_ASSERTS

TEST(TessAssert, MacroIsCompiledOutExactlyWhenAssertsDisabled) {
#if TESS_ENABLE_ASSERTS
  EXPECT_TRUE(TESS_ENABLE_ASSERTS);
#else
  // The disabled forms must still swallow the condition expression.
  bool evaluated = false;
  TESS_ASSERT((evaluated = true));
  EXPECT_FALSE(evaluated);
  TESS_ASSERT_MSG((evaluated = true), "unused message");
  EXPECT_FALSE(evaluated);
#endif
}

TEST(TessAssert, AssertMsgPassesWithoutSideEffectsWhenConditionHolds) {
  int evaluations = 0;
  TESS_ASSERT_MSG(++evaluations > 0, "condition must be evaluated once");
#if TESS_ENABLE_ASSERTS
  EXPECT_EQ(evaluations, 1);
#else
  EXPECT_EQ(evaluations, 0);
#endif
}

TEST(TessAssert, UncheckedAccessorsStayNoexcept) {
  static_assert(noexcept(std::declval<World&>().resolve(tess::Coord3{})));
  static_assert(noexcept(std::declval<World&>().chunk(tess::ChunkKey{})));
  static_assert(noexcept(std::declval<World&>().meta(tess::ChunkKey{})));
  SUCCEED();
}

#if TESS_ENABLE_ASSERTS

using TessAssertDeathTest = ::testing::Test;

TEST(TessAssertDeathTest, AssertMsgAbortsWithTheCustomMessage) {
  // TESS_ASSERT_MSG replaces the stringified condition with the caller's
  // message in the abort diagnostic.
  bool condition = false;
  EXPECT_DEATH(TESS_ASSERT_MSG(condition, "custom precondition message"),
               "tess assertion failed: custom precondition message");
}

TEST(TessAssertDeathTest, ResolveRejectsNegativeCoordinate) {
  World world;
  EXPECT_DEATH(static_cast<void>(world.resolve(tess::Coord3{-1, 0, 0})),
               kAssertDeathMessage);
}

TEST(TessAssertDeathTest, ResolveRejectsCoordinateBeyondShape) {
  World world;
  EXPECT_DEATH(static_cast<void>(world.resolve(tess::Coord3{128, 0, 0})),
               kAssertDeathMessage);
}

TEST(TessAssertDeathTest, FieldRejectsNegativeCoordinate) {
  World world;
  EXPECT_DEATH(
      static_cast<void>(world.field<TerrainTag>(tess::Coord3{0, -1, 0})),
      kAssertDeathMessage);
}

TEST(TessAssertDeathTest, ChunkRejectsOutOfRangeKey) {
  World world;
  EXPECT_DEATH(
      static_cast<void>(world.chunk(tess::ChunkKey{World::chunk_count})),
      kAssertDeathMessage);
}

TEST(TessAssertDeathTest, MetaRejectsOutOfRangeKey) {
  World world;
  EXPECT_DEATH(
      static_cast<void>(world.meta(tess::ChunkKey{World::chunk_count})),
      kAssertDeathMessage);
}

TEST(TessAssertDeathTest, TileKeyRejectsCoordinateOutsideShape) {
  EXPECT_DEATH(
      static_cast<void>(tess::tile_key<TopDown2D>(tess::Coord3{-1, 0, 0})),
      kAssertDeathMessage);
}

TEST(TessAssertDeathTest, RuntimeResultRejectsOutOfRangeTicket) {
  tess::PathRequestRuntime runtime;
  EXPECT_DEATH(static_cast<void>(runtime.result(tess::PathTicket{7})),
               kAssertDeathMessage);
}

TEST(TessAssertDeathTest, RuntimeResultRejectsStaleTicketGeneration) {
  PathWorld world;
  fill_passable(world);
  tess::PathRequestRuntime runtime;

  const auto stale = runtime.submit(
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{1, 0, 0}});
  (void)runtime.process_unit_cached<PathWorld, PassableTag>(world);
  ASSERT_EQ(runtime.result(stale).status, tess::PathStatus::Found);

  // Same-size resubmission: the stale ticket aliases the new request's
  // slot, so a range check alone cannot catch the reuse.
  runtime.clear_requests();
  (void)runtime.submit(
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{2, 0, 0}});
  (void)runtime.process_unit_cached<PathWorld, PassableTag>(world);
  EXPECT_DEATH(static_cast<void>(runtime.result(stale)), kAssertDeathMessage);
}

#endif  // TESS_ENABLE_ASSERTS

}  // namespace
