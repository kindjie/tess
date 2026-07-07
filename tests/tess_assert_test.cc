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

constexpr auto kAssertDeathMessage = "tess assertion failed";

TEST(TessAssert, MacroIsCompiledOutExactlyWhenAssertsDisabled) {
#if TESS_ENABLE_ASSERTS
  EXPECT_TRUE(TESS_ENABLE_ASSERTS);
#else
  // The disabled form must still swallow the condition expression.
  bool evaluated = false;
  TESS_ASSERT((evaluated = true));
  EXPECT_FALSE(evaluated);
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

#endif  // TESS_ENABLE_ASSERTS

}  // namespace
