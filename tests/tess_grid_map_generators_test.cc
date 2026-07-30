#include <gtest/gtest.h>
#include <tess/tess.h>

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
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
  const char* name = nullptr;
  std::string text;
};

// The generators return optionals for out-of-contract extents; every
// call here is in contract, and value_or keeps the accesses checked.
auto maze_text(std::size_t width, std::size_t height, std::uint64_t seed)
    -> std::string {
  return grid::recursive_division_maze(width, height, seed)
      .value_or(std::string{});
}

auto room_map(std::size_t width, std::size_t height, std::uint64_t seed,
              grid::RoomParams params = {}) -> grid::RoomMapResult {
  return grid::room_and_corridor(width, height, seed, params)
      .value_or(grid::RoomMapResult{});
}

auto generated_cases() -> const std::vector<GeneratedCase>& {
  static const std::vector<GeneratedCase> cases = [] {
    std::vector<GeneratedCase> out;
    out.push_back({"maze_33x33", maze_text(33, 33, 0xA1)});
    out.push_back({"maze_48x37", maze_text(48, 37, 0xB2)});
    out.push_back({"rooms_64x64", room_map(64, 64, 0xC3, {12, 4, 10}).text});
    out.push_back({"rooms_40x24", room_map(40, 24, 0xD4, {12, 4, 10}).text});
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
    EXPECT_FALSE(maze_text(33, 33, 0xA1).empty());
    EXPECT_EQ(maze_text(33, 33, 0xA1), maze_text(33, 33, 0xA1));
    const auto a = room_map(40, 24, 0xD4);
    const auto b = room_map(40, 24, 0xD4);
    EXPECT_FALSE(a.text.empty());
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
  // Cross-platform golden for the whole generation pipeline: PRNG
  // stream, division order, wall/gap placement, and emission. A
  // platform or algorithm change that moves any of them fails here.
  // If the change is intentional, update this literal in the same
  // commit and say so in the message.
  static constexpr std::string_view kGolden =
      "type octile\n"
      "height 9\n"
      "width 9\n"
      "map\n"
      ".@.......\n"
      ".@.@.@.@.\n"
      "...@.@.@.\n"
      ".@.@@@@@@\n"
      ".@...@...\n"
      ".@.@@@@@.\n"
      ".@.......\n"
      ".@.@@@@@.\n"
      ".@...@...\n";

  const auto text = maze_text(9, 9, 42);

  EXPECT_EQ(text, kGolden);
  const auto parsed = grid::parse_map("golden.map", text);
  ASSERT_TRUE(parsed);
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
  const auto result = room_map(64, 64, 0xC3, {12, 4, 10});
  EXPECT_GE(result.rooms, 1u);
  const auto parsed = grid::parse_map("rooms.map", result.text);
  ASSERT_TRUE(parsed);
  // The guaranteed first room provides at least min_extent^2 floor.
  const auto flood = grid::flood_fill(parsed.value);
  EXPECT_GE(flood.passable, 16u);
}

TEST(TessGridMapGenerators, EndpointSamplingIsDeterministicAndValid) {
  const auto parsed = grid::parse_map("maze.map", maze_text(33, 33, 0xA1));
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

  // Sentinel: a reachable pair never yields nullopt, and the sentinel
  // can never equal a real cost, so an absent reference still fails.
  static constexpr std::uint64_t kNoReference =
      std::numeric_limits<std::uint64_t>::max();

  const auto orthogonal_reference =
      grid::reference_cost(map, start, goal,
                           grid::ReferenceMovement::Orthogonal)
          .value_or(kNoReference);
  const auto orthogonal = tess::astar_path<decltype(world), PassableTag>(
      world, {start, goal}, scratch);
  ASSERT_EQ(orthogonal.status, tess::PathStatus::Found);
  EXPECT_EQ(orthogonal.cost, orthogonal_reference);

  const auto diagonal_reference =
      grid::reference_cost(map, start, goal,
                           grid::ReferenceMovement::DiagonalBothClear)
          .value_or(kNoReference);
  const auto diagonal = tess::astar_path<decltype(world), Diagonal>(
      world, {start, goal}, scratch);
  ASSERT_EQ(diagonal.status, tess::PathStatus::Found);
  EXPECT_EQ(diagonal.cost, diagonal_reference);
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
