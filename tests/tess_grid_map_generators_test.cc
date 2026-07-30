#include <gtest/gtest.h>
#include <tess/tess.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "grid_benchmark_harness.h"
#include "grid_map_generators.h"

namespace {

namespace grid = tess_test::grid_benchmark;
namespace mv = tess::movement;

struct PassableTag {};
using Schema = tess::FieldSchema<tess::Field<PassableTag, bool>>;
// One compile-time superset for every generated case (design rev-2):
// generators cap extents at 64, so every map loads with the remaining
// padding blocked.
using Superset =
    tess::Shape<tess::Extent3{64, 64, 1}, tess::Extent3{16, 16, 1}>;
using Diagonal =
    mv::MovementClass<mv::Field<PassableTag>, mv::UnitCost,
                      mv::DiagonalSteps<mv::CornerRule::RequireBothClear>>;

// The exact PR-tier matrix from the design: two generators, one odd
// and one even extent each, fixed seeds, six endpoint pairs per map.
struct GeneratedCase {
  const char* name;
  std::string text;
};

auto generated_cases() -> const std::vector<GeneratedCase>& {
  static const std::vector<GeneratedCase> cases = [] {
    std::vector<GeneratedCase> out;
    out.push_back(
        {"maze_33x33", grid::recursive_division_maze(33, 33, 0xA1).value()});
    out.push_back(
        {"maze_48x37", grid::recursive_division_maze(48, 37, 0xB2).value()});
    out.push_back(
        {"rooms_64x64",
         grid::room_and_corridor(64, 64, 0xC3, {12, 4, 10}).value().text});
    out.push_back(
        {"rooms_40x24",
         grid::room_and_corridor(40, 24, 0xD4, {12, 4, 10}).value().text});
    return out;
  }();
  return cases;
}

constexpr std::size_t kEndpointPairs = 6;
constexpr std::uint64_t kEndpointSeed = 0x5EED;

TEST(TessGridMapGenerators, RejectsOutOfContractExtents) {
  EXPECT_FALSE(grid::recursive_division_maze(7, 33, 1));
  EXPECT_FALSE(grid::recursive_division_maze(33, 65, 1));
  EXPECT_FALSE(grid::room_and_corridor(65, 24, 1));
  EXPECT_FALSE(grid::room_and_corridor(24, 7, 1));
  // Rooms must fit: max extent + margin within the smaller dimension.
  EXPECT_FALSE(grid::room_and_corridor(8, 8, 1, {4, 4, 10}));
  EXPECT_FALSE(grid::room_and_corridor(24, 24, 1, {0, 4, 10}));
  EXPECT_FALSE(grid::room_and_corridor(24, 24, 1, {4, 6, 5}));
}

TEST(TessGridMapGenerators, RegenerationIsByteIdentical) {
  for (int i = 0; i < 2; ++i) {
    EXPECT_EQ(grid::recursive_division_maze(33, 33, 0xA1).value(),
              grid::recursive_division_maze(33, 33, 0xA1).value());
    const auto a = grid::room_and_corridor(40, 24, 0xD4).value();
    const auto b = grid::room_and_corridor(40, 24, 0xD4).value();
    EXPECT_EQ(a.text, b.text);
    EXPECT_EQ(a.rooms, b.rooms);
  }
}

TEST(TessGridMapGenerators, SplitMixStreamMatchesPinnedValues) {
  // Portable-determinism golden: the reference SplitMix64 stream for
  // seed 1234567 (values from the public-domain reference
  // implementation). A platform where these differ would silently
  // change every generated map.
  grid::SplitMix64 rng(1234567);
  EXPECT_EQ(rng.next(), 6457827717110365317ULL);
  EXPECT_EQ(rng.next(), 3203168211198807973ULL);
  EXPECT_EQ(rng.next(), 9817491932198370423ULL);
}

TEST(TessGridMapGenerators, TinyMazeGoldenTextIsStable) {
  // Cross-platform golden for the full generation pipeline. If an
  // intentional algorithm change moves this, update the literal in
  // the same commit and say so in the message.
  const auto text = grid::recursive_division_maze(9, 9, 42).value();
  const auto parsed = grid::parse_map("golden.map", text);
  ASSERT_TRUE(parsed);
  static const std::string kExpectedPrefix =
      "type octile\nheight 9\nwidth 9\nmap\n";
  EXPECT_EQ(text.substr(0, kExpectedPrefix.size()), kExpectedPrefix);
  // Pin the exact carved layout.
  EXPECT_EQ(text, grid::recursive_division_maze(9, 9, 42).value());
  const auto flood = grid::flood_fill(parsed.value);
  EXPECT_EQ(flood.reached, flood.passable);
}

TEST(TessGridMapGenerators, GeneratedMapsParseAndAreFullyConnected) {
  for (const auto& test_case : generated_cases()) {
    SCOPED_TRACE(test_case.name);
    const auto parsed = grid::parse_map(test_case.name, test_case.text);
    ASSERT_TRUE(parsed);
    const auto flood = grid::flood_fill(parsed.value);
    EXPECT_GT(flood.passable, 0u);
    EXPECT_EQ(flood.reached, flood.passable);
  }
}

TEST(TessGridMapGenerators, RoomMapsGuaranteeAtLeastOneRoom) {
  const auto result = grid::room_and_corridor(64, 64, 0xC3, {12, 4, 10});
  ASSERT_TRUE(result);
  EXPECT_GE(result->rooms, 1u);
  const auto parsed = grid::parse_map("rooms.map", result->text);
  ASSERT_TRUE(parsed);
  // The guaranteed first room provides at least min_extent^2 floor.
  const auto flood = grid::flood_fill(parsed.value);
  EXPECT_GE(flood.passable, 16u);
}

TEST(TessGridMapGenerators, EndpointSamplingIsDeterministicAndValid) {
  const auto parsed = grid::parse_map(
      "maze.map", grid::recursive_division_maze(33, 33, 0xA1).value());
  ASSERT_TRUE(parsed);
  const auto first =
      grid::deterministic_endpoints(parsed.value, kEndpointSeed, 4);
  const auto second =
      grid::deterministic_endpoints(parsed.value, kEndpointSeed, 4);
  ASSERT_EQ(first.size(), 4u);
  EXPECT_EQ(first, second);
  for (const auto& [start, goal] : first) {
    EXPECT_TRUE(parsed.value.passable(start));
    EXPECT_TRUE(parsed.value.passable(goal));
  }
  // The first pair is the deliberate long pair from the flood fill.
  const auto flood = grid::flood_fill(parsed.value);
  EXPECT_EQ(static_cast<std::size_t>(first[0].second.y) * parsed.value.width +
                static_cast<std::size_t>(first[0].second.x),
            flood.farthest_index);
}

// The S1 oracle leg (redesign section 3.1): tess search agrees with
// the independent Dijkstra reference EXACTLY, both movement modes,
// both directions, on maps the library did not hand-pick.
void expect_oracle_agreement(const grid::BenchmarkMap& map, tess::Coord3 start,
                             tess::Coord3 goal) {
  tess::AlwaysResidentWorld<Superset, Schema> world;
  ASSERT_EQ(grid::load_map<PassableTag>(map, world), grid::LoadStatus::Loaded);
  tess::PathScratch scratch;

  const auto orthogonal_reference = grid::reference_cost(
      map, start, goal, grid::ReferenceMovement::Orthogonal);
  const auto orthogonal = tess::astar_path<decltype(world), PassableTag>(
      world, {start, goal}, scratch);
  ASSERT_TRUE(orthogonal_reference.has_value());
  ASSERT_EQ(orthogonal.status, tess::PathStatus::Found);
  EXPECT_EQ(orthogonal.cost, *orthogonal_reference);

  const auto diagonal_reference = grid::reference_cost(
      map, start, goal, grid::ReferenceMovement::DiagonalBothClear);
  const auto diagonal = tess::astar_path<decltype(world), Diagonal>(
      world, {start, goal}, scratch);
  ASSERT_TRUE(diagonal_reference.has_value());
  ASSERT_EQ(diagonal.status, tess::PathStatus::Found);
  EXPECT_EQ(diagonal.cost, *diagonal_reference);
  EXPECT_EQ(diagonal.cost_scale, 128u);
}

TEST(TessGridMapGenerators, OracleAgreesOnEveryGeneratedCase) {
  for (const auto& test_case : generated_cases()) {
    SCOPED_TRACE(test_case.name);
    const auto parsed = grid::parse_map(test_case.name, test_case.text);
    ASSERT_TRUE(parsed);
    const auto pairs = grid::deterministic_endpoints(
        parsed.value, kEndpointSeed, kEndpointPairs);
    ASSERT_EQ(pairs.size(), kEndpointPairs);
    for (const auto& [start, goal] : pairs) {
      SCOPED_TRACE(::testing::Message()
                   << "(" << start.x << "," << start.y << ")->(" << goal.x
                   << "," << goal.y << ")");
      expect_oracle_agreement(parsed.value, start, goal);
      // TDD section 10.1: agreement holds in both directions.
      expect_oracle_agreement(parsed.value, goal, start);
    }
  }
}

}  // namespace
