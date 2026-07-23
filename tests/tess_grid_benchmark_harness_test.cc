#include <gtest/gtest.h>
#include <tess/tess.h>

#include <cmath>
#include <string>
#include <string_view>

#include "grid_benchmark_harness.h"

namespace {

namespace grid = tess_test::grid_benchmark;
namespace mv = tess::movement;

struct PassableTag {};
using Schema = tess::FieldSchema<tess::Field<PassableTag, bool>>;
using Padded = tess::Shape<tess::Extent3{8, 8, 1}, tess::Extent3{4, 4, 1}>;
using Diagonal =
    mv::MovementClass<mv::Field<PassableTag>, mv::UnitCost,
                      mv::DiagonalSteps<mv::CornerRule::RequireBothClear>>;

constexpr std::string_view kMap = R"(type octile
height 3
width 5
map
.@...
..T..
...G.
)";

TEST(TessGridBenchmarkHarness, ParsesTerrainAndTopLeftCoordinates) {
  const auto parsed = grid::parse_map("fixture.map", kMap);

  ASSERT_TRUE(parsed);
  EXPECT_EQ(parsed.value.width, 5u);
  EXPECT_EQ(parsed.value.height, 3u);
  EXPECT_TRUE(parsed.value.passable({0, 0, 0}));
  EXPECT_FALSE(parsed.value.passable({1, 0, 0}));
  EXPECT_FALSE(parsed.value.passable({2, 1, 0}));
  EXPECT_TRUE(parsed.value.passable({3, 2, 0}));
}

TEST(TessGridBenchmarkHarness, RejectsUnsupportedTerrainAndHeaders) {
  const auto swamp =
      grid::parse_map("swamp.map", "type octile\nheight 1\nwidth 1\nmap\nS\n");
  const auto wrong_type =
      grid::parse_map("grid.map", "type grid\nheight 1\nwidth 1\nmap\n.\n");
  const auto short_row =
      grid::parse_map("short.map", "type octile\nheight 1\nwidth 2\nmap\n.\n");

  EXPECT_EQ(swamp.error, grid::ParseError::UnsupportedTerrain);
  EXPECT_EQ(wrong_type.error, grid::ParseError::InvalidHeader);
  EXPECT_EQ(short_row.error, grid::ParseError::InvalidDimensions);
}

TEST(TessGridBenchmarkHarness, ParsesAndValidatesScenarioRows) {
  const auto map = grid::parse_map("fixture.map", kMap).value;
  const auto parsed = grid::parse_scenarios(
      "version 1.0\n0 fixture.map 5 3 0 0 4 2 4.82843\n", map);

  ASSERT_TRUE(parsed);
  ASSERT_EQ(parsed.value.size(), 1u);
  EXPECT_EQ(parsed.value[0].start, (tess::Coord3{0, 0, 0}));
  EXPECT_EQ(parsed.value[0].goal, (tess::Coord3{4, 2, 0}));
  EXPECT_EQ(parsed.value[0].fractional_digits, 5u);

  EXPECT_EQ(grid::parse_scenarios(
                "version 1.0\n0 other.map 5 3 0 0 4 2 4.82843\n", map)
                .error,
            grid::ParseError::ScenarioMapMismatch);
  EXPECT_EQ(grid::parse_scenarios(
                "version 1.0\n0 fixture.map 5 3 1 0 4 2 4.82843\n", map)
                .error,
            grid::ParseError::BlockedEndpoint);
}

TEST(TessGridBenchmarkHarness, RejectsInvalidScenarioLengths) {
  const auto map = grid::parse_map("fixture.map", kMap).value;

  for (const auto length : {"nan.0", "inf.0", "4.82843x"}) {
    const auto scenario =
        "version 1.0\n0 fixture.map 5 3 0 0 4 2 " + std::string{length} + "\n";
    EXPECT_EQ(grid::parse_scenarios(scenario, map).error,
              grid::ParseError::InvalidScenario);
  }
}

TEST(TessGridBenchmarkHarness, LoadsIntoDeclaredShapeAndBlocksPadding) {
  const auto map = grid::parse_map("fixture.map", kMap).value;
  tess::AlwaysResidentWorld<Padded, Schema> world;

  ASSERT_EQ(grid::load_map<PassableTag>(map, world), grid::LoadStatus::Loaded);
  EXPECT_TRUE(world.field<PassableTag>({0, 0, 0}));
  EXPECT_FALSE(world.field<PassableTag>({1, 0, 0}));
  EXPECT_TRUE(world.field<PassableTag>({4, 2, 0}));
  EXPECT_FALSE(world.field<PassableTag>({5, 2, 0}));
  EXPECT_FALSE(world.field<PassableTag>({0, 3, 0}));
}

TEST(TessGridBenchmarkHarness, RejectsMapsLargerThanDeclaredShape) {
  const auto map =
      grid::parse_map("wide.map",
                      "type octile\nheight 1\nwidth 9\nmap\n.........\n")
          .value;
  tess::AlwaysResidentWorld<Padded, Schema> world;

  EXPECT_EQ(grid::load_map<PassableTag>(map, world),
            grid::LoadStatus::UnsupportedExtent);
}

TEST(TessGridBenchmarkHarness, ReferenceSearchPinsCostsAndCornerRule) {
  const auto open =
      grid::parse_map("open.map",
                      "type octile\nheight 3\nwidth 3\nmap\n...\n...\n...\n")
          .value;
  const auto corner =
      grid::parse_map("corner.map",
                      "type octile\nheight 2\nwidth 2\nmap\n.@\n..\n")
          .value;

  EXPECT_EQ(grid::reference_cost(open, {0, 0, 0}, {2, 2, 0},
                                 grid::ReferenceMovement::Orthogonal),
            4u);
  EXPECT_EQ(grid::reference_cost(open, {0, 0, 0}, {2, 2, 0},
                                 grid::ReferenceMovement::DiagonalBothClear),
            362u);
  EXPECT_EQ(grid::reference_cost(corner, {0, 0, 0}, {1, 1, 0},
                                 grid::ReferenceMovement::DiagonalBothClear),
            256u);
}

TEST(TessGridBenchmarkHarness, TessSearchMatchesIndependentReference) {
  const auto map =
      grid::parse_map("open.map",
                      "type octile\nheight 3\nwidth 3\nmap\n...\n...\n...\n")
          .value;
  tess::AlwaysResidentWorld<Padded, Schema> world;
  ASSERT_EQ(grid::load_map<PassableTag>(map, world), grid::LoadStatus::Loaded);
  constexpr auto start = tess::Coord3{0, 0, 0};
  constexpr auto goal = tess::Coord3{2, 2, 0};
  tess::PathScratch orthogonal_scratch;
  tess::PathScratch diagonal_scratch;

  const auto orthogonal = tess::astar_path<decltype(world), PassableTag>(
      world, {start, goal}, orthogonal_scratch);
  const auto diagonal = tess::astar_path<decltype(world), Diagonal>(
      world, {start, goal}, diagonal_scratch);

  ASSERT_EQ(orthogonal.status, tess::PathStatus::Found);
  ASSERT_EQ(diagonal.status, tess::PathStatus::Found);
  EXPECT_EQ(orthogonal.cost,
            grid::reference_cost(map, start, goal,
                                 grid::ReferenceMovement::Orthogonal));
  EXPECT_EQ(diagonal.cost,
            grid::reference_cost(map, start, goal,
                                 grid::ReferenceMovement::DiagonalBothClear));
  EXPECT_EQ(diagonal.cost_scale, 128u);
}

TEST(TessGridBenchmarkHarness, ExternalIntervalIsAsymmetricAndDecimalBounded) {
  const auto interval = grid::external_cost_interval(100.0, 5);

  ASSERT_TRUE(interval);
  EXPECT_LT(interval->lower, 100.0);
  EXPECT_NEAR(interval->upper, 100.00001, 1.0e-12);
  EXPECT_NEAR(interval->lower,
              100.0 * (181.0 / (128.0 * std::sqrt(2.0))) - 0.00001, 1.0e-12);
  EXPECT_FALSE(grid::external_cost_interval(100.0, 3));
}

}  // namespace
