#include <gtest/gtest.h>
#include <tess/tess.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>

// S5.2: movement classes threaded through the A* leaves and weighted cores.
// The identity/LegacyWeighted equivalence cases pin the byte-identity
// contract (a class-driven search returns node-for-node what the raw-tag
// search returns); the Walker/Builder cases prove two classes over one world
// actually diverge where their predicates differ.
namespace {

namespace mv = tess::movement;

struct PassableTag {};
struct ConstructionTag {};
struct CostTag {};
struct OccupancyTag {};
struct ReservationTag {};
struct StairTag {};

using Schema = tess::FieldSchema<
    tess::Field<PassableTag, bool>, tess::Field<ConstructionTag, std::uint8_t>,
    tess::Field<CostTag, std::uint32_t>, tess::Field<OccupancyTag, bool>,
    tess::Field<ReservationTag, bool>>;
using TopDown2D = tess::Shape<tess::Extent3{8, 8, 1}, tess::Extent3{4, 4, 1}>;
using World = tess::AlwaysResidentWorld<TopDown2D, Schema>;

// Walker: passable terrain not under construction, plain terrain cost.
using Walker = mv::MovementClass<
    mv::AllOf<mv::Field<PassableTag>, mv::Not<mv::Field<ConstructionTag>>>,
    mv::FieldCost<CostTag>>;
// Builder: may also enter construction tiles at a fixed build price.
using Builder = mv::MovementClass<
    mv::AnyOf<mv::Field<PassableTag>, mv::Field<ConstructionTag>>,
    mv::SelectCost<ConstructionTag, mv::ConstantCost<3>,
                   mv::FieldCost<CostTag>>>;
using DefaultClass =
    mv::MovementClass<mv::Field<PassableTag>, mv::FieldCost<CostTag>>;
using Diagonal =
    mv::MovementClass<mv::Field<PassableTag>, mv::FieldCost<CostTag>,
                      mv::DiagonalSteps<mv::CornerRule::RequireBothClear>>;

void fill_open(World& world, std::uint32_t cost) {
  for (auto& page : world.chunks()) {
    auto passable = page.template field_span<PassableTag>();
    std::fill(passable.begin(), passable.end(), true);
    auto costs = page.template field_span<CostTag>();
    std::fill(costs.begin(), costs.end(), cost);
  }
}

void expect_same_result(const tess::PathResult& lhs,
                        const tess::PathResult& rhs) {
  EXPECT_EQ(lhs.status, rhs.status);
  EXPECT_EQ(lhs.cost, rhs.cost);
  EXPECT_EQ(lhs.expanded_nodes, rhs.expanded_nodes);
  EXPECT_EQ(lhs.reached_nodes, rhs.reached_nodes);
  ASSERT_EQ(lhs.path.size(), rhs.path.size());
  for (std::size_t i = 0; i < lhs.path.size(); ++i) {
    EXPECT_EQ(lhs.path[i], rhs.path[i]) << "node " << i;
  }
}

// A serpentine wall pattern that defeats the straight-line fast paths, so the
// equivalence claims below cover the real heap loops.
void carve_serpentine(World& world) {
  for (std::int64_t y = 1; y < 8; y += 2) {
    const auto open_x = (y % 4 == 1) ? std::int64_t{7} : std::int64_t{0};
    for (std::int64_t x = 0; x < 8; ++x) {
      if (x != open_x) {
        world.field<PassableTag>(tess::Coord3{x, y, 0}) = false;
      }
    }
  }
}

TEST(TessPathMovementClass, IdentityClassMatchesRawTagUnitSearch) {
  World world;
  fill_open(world, 1);
  carve_serpentine(world);

  tess::PathScratch tag_scratch;
  tess::PathScratch class_scratch;
  const auto request =
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{7, 6, 0}};

  const auto by_tag =
      tess::astar_path<World, PassableTag>(world, request, tag_scratch);
  const auto by_class = tess::astar_path<World, mv::WalkableField<PassableTag>>(
      world, request, class_scratch);

  ASSERT_EQ(by_tag.status, tess::PathStatus::Found);
  expect_same_result(by_tag, by_class);
}

TEST(TessPathMovementClass, ResultAggregateDefaultsToUnitCostScale) {
  const auto result = tess::PathResult{tess::PathStatus::Found, 4, 5, 6, {}};
  EXPECT_EQ(result.cost_scale, 1u);
}

TEST(TessPathMovementClass, DiagonalSearchReturnsFixedPointCostScale) {
  World world;
  fill_open(world, 1);
  tess::PathScratch scratch;

  const auto result = tess::weighted_astar_path<World, Diagonal>(
      world, tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{3, 3, 0}},
      scratch);

  ASSERT_EQ(result.status, tess::PathStatus::Found);
  EXPECT_EQ(result.cost, 3u * 181u);
  EXPECT_EQ(result.cost_scale, 128u);
  EXPECT_EQ(result.path.size(), 4u);
}

TEST(TessPathMovementClass, UnitDiagonalSearchIgnoresEntryCostExpression) {
  World world;
  fill_open(world, 99);
  tess::PathScratch scratch;

  const auto result = tess::astar_path<World, Diagonal>(
      world, tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{3, 3, 0}},
      scratch);

  ASSERT_EQ(result.status, tess::PathStatus::Found);
  EXPECT_EQ(result.cost, 3u * 181u);
  EXPECT_EQ(result.cost_scale, 128u);
}

TEST(TessPathMovementClass, DiagonalReverseFieldMatchesExactSearch) {
  World world;
  fill_open(world, 1);
  tess::PathScratch path_scratch;
  tess::DistanceFieldScratch field_scratch;
  constexpr auto start = tess::Coord3{0, 0, 0};
  constexpr auto goal = tess::Coord3{3, 3, 0};
  const auto exact = tess::weighted_astar_path<World, Diagonal>(
      world, tess::PathRequest{start, goal}, path_scratch);

  const auto built = tess::build_weighted_distance_field<World, Diagonal>(
      world, goal, field_scratch);
  ASSERT_EQ(built.status, tess::PathStatus::Found);
  const auto from_field = tess::weighted_distance_field_path<World, Diagonal>(
      world, start, goal, field_scratch);

  ASSERT_EQ(from_field.status, tess::PathStatus::Found);
  EXPECT_EQ(from_field.cost, exact.cost);
  EXPECT_EQ(from_field.cost_scale, 128u);
  ASSERT_EQ(from_field.path.size(), exact.path.size());
  for (std::size_t i = 0; i < exact.path.size(); ++i) {
    EXPECT_EQ(from_field.path[i], exact.path[i]);
  }
}

TEST(TessPathMovementClass, DistanceFieldRejectsAnotherResolvedModel) {
  World world;
  fill_open(world, 1);
  tess::DistanceFieldScratch scratch;
  constexpr auto goal = tess::Coord3{3, 3, 0};

  ASSERT_EQ((tess::build_weighted_distance_field<World, DefaultClass>(
                 world, goal, scratch)
                 .status),
            tess::PathStatus::Found);
  const auto mismatched = tess::weighted_distance_field_path<World, Diagonal>(
      world, goal, goal, scratch);

  EXPECT_EQ(mismatched.status, tess::PathStatus::NoPath);
}

TEST(TessPathMovementClass, AxialHexSearchUsesSixRegularNeighbors) {
  using HexShape = tess::Shape<tess::Extent3{8, 8, 1}, tess::Extent3{4, 4, 1},
                               tess::lattice::HexAxial>;
  using HexWorld = tess::AlwaysResidentWorld<HexShape, Schema>;
  HexWorld world;
  for (auto& page : world.chunks()) {
    std::fill(page.field_span<PassableTag>().begin(),
              page.field_span<PassableTag>().end(), true);
    std::fill(page.field_span<CostTag>().begin(),
              page.field_span<CostTag>().end(), 1u);
  }
  tess::PathScratch scratch;

  const auto result = tess::weighted_astar_path<HexWorld, DefaultClass>(
      world, tess::PathRequest{tess::Coord3{1, 2, 0}, tess::Coord3{3, 0, 0}},
      scratch);

  ASSERT_EQ(result.status, tess::PathStatus::Found);
  EXPECT_EQ(result.cost, 2u);
  EXPECT_EQ(result.cost_scale, 1u);
  ASSERT_EQ(result.path.size(), 3u);
  EXPECT_EQ(result.path[1], (tess::Coord3{2, 1, 0}));

  tess::PathScratch unit_scratch;
  const auto unit = tess::astar_path<HexWorld, PassableTag>(
      world, tess::PathRequest{tess::Coord3{1, 2, 0}, tess::Coord3{3, 0, 0}},
      unit_scratch);
  ASSERT_EQ(unit.status, tess::PathStatus::Found);
  EXPECT_EQ(unit.cost, 2u);
  EXPECT_EQ(unit.path.size(), 3u);
}

TEST(TessPathMovementClass, ReportsUnrepresentableExactCost) {
  World world;
  fill_open(world, 1);
  world.field<CostTag>(tess::Coord3{1, 0, 0}) =
      std::numeric_limits<std::uint32_t>::max();
  tess::PathScratch scratch;

  const auto result = tess::weighted_astar_path<World, DefaultClass>(
      world, tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{1, 0, 0}},
      scratch);

  EXPECT_EQ(result.status, tess::PathStatus::CostOverflow);
  EXPECT_EQ(result.cost_scale, 1u);
}

TEST(TessPathMovementClass, ProviderAwareSearchUsesCheaperStairEdge) {
  using StairSchema = tess::FieldSchema<tess::Field<PassableTag, bool>,
                                        tess::Field<CostTag, std::uint32_t>,
                                        tess::Field<StairTag, std::uint8_t>>;
  using StairShape =
      tess::Shape<tess::Extent3{4, 4, 2}, tess::Extent3{4, 4, 2}>;
  using StairWorld = tess::AlwaysResidentWorld<StairShape, StairSchema>;
  StairWorld world;
  for (auto& page : world.chunks()) {
    std::fill(page.field_span<PassableTag>().begin(),
              page.field_span<PassableTag>().end(), true);
    std::fill(page.field_span<CostTag>().begin(),
              page.field_span<CostTag>().end(), 1u);
  }
  constexpr auto foot = tess::Coord3{1, 1, 0};
  constexpr auto landing = tess::Coord3{2, 1, 1};
  world.field<StairTag>(foot) =
      static_cast<std::uint8_t>(tess::StairDirection::PositiveX);
  const auto provider = tess::StairTransitions<StairTag>{};
  const auto request = tess::PathRequest{foot, landing};
  tess::PathScratch regular_scratch;
  tess::PathScratch weighted_scratch;
  tess::PathScratch unit_scratch;

  const auto regular = tess::weighted_astar_path<StairWorld, DefaultClass>(
      world, request, regular_scratch);
  const auto weighted = tess::weighted_astar_path<StairWorld, DefaultClass>(
      world, request, weighted_scratch,
      tess::MissingChunkPolicy::TreatAsBlocked, provider);
  const auto unit = tess::astar_path<StairWorld, PassableTag>(
      world, request, unit_scratch, tess::MissingChunkPolicy::TreatAsBlocked,
      provider);

  ASSERT_EQ(regular.status, tess::PathStatus::Found);
  EXPECT_EQ(regular.cost, 2u);
  ASSERT_EQ(weighted.status, tess::PathStatus::Found);
  EXPECT_EQ(weighted.cost, 1u);
  ASSERT_EQ(weighted.path.size(), 2u);
  EXPECT_EQ(weighted.path.back(), landing);
  ASSERT_EQ(unit.status, tess::PathStatus::Found);
  EXPECT_EQ(unit.cost, 1u);
  EXPECT_EQ(unit.path.size(), 2u);
}

TEST(TessPathMovementClass, ProviderAwareFieldUsesAndStampsStairEdge) {
  using StairSchema = tess::FieldSchema<tess::Field<PassableTag, bool>,
                                        tess::Field<CostTag, std::uint32_t>,
                                        tess::Field<StairTag, std::uint8_t>>;
  using StairShape =
      tess::Shape<tess::Extent3{4, 4, 2}, tess::Extent3{4, 4, 2}>;
  using StairWorld = tess::AlwaysResidentWorld<StairShape, StairSchema>;
  StairWorld world;
  for (auto& page : world.chunks()) {
    std::fill(page.field_span<PassableTag>().begin(),
              page.field_span<PassableTag>().end(), true);
    std::fill(page.field_span<CostTag>().begin(),
              page.field_span<CostTag>().end(), 1u);
  }
  constexpr auto foot = tess::Coord3{1, 1, 0};
  constexpr auto landing = tess::Coord3{2, 1, 1};
  world.field<StairTag>(foot) =
      static_cast<std::uint8_t>(tess::StairDirection::PositiveX);
  const auto provider = tess::StairTransitions<StairTag>{};
  tess::DistanceFieldScratch scratch;

  const auto built =
      tess::build_weighted_distance_field<StairWorld, DefaultClass>(
          world, landing, scratch, tess::MissingChunkPolicy::TreatAsBlocked,
          provider);
  ASSERT_EQ(built.status, tess::PathStatus::Found);
  const auto result =
      tess::weighted_distance_field_path<StairWorld, DefaultClass>(
          world, foot, landing, scratch, provider);
  ASSERT_EQ(result.status, tess::PathStatus::Found);
  EXPECT_EQ(result.cost, 1u);
  EXPECT_EQ(result.path.size(), 2u);

  const auto mismatched =
      tess::weighted_distance_field_path<StairWorld, DefaultClass>(
          world, foot, landing, scratch);
  EXPECT_EQ(mismatched.status, tess::PathStatus::NoPath);

  const auto boxed =
      tess::build_weighted_distance_field_in_box<StairWorld, DefaultClass>(
          world, landing,
          tess::Box3{tess::Coord3{0, 0, 0}, tess::Extent3{4, 4, 2}}, scratch,
          tess::MissingChunkPolicy::TreatAsBlocked, provider);
  ASSERT_EQ(boxed.status, tess::PathStatus::Found);
  EXPECT_EQ((tess::weighted_distance_field_path<StairWorld, DefaultClass>(
                 world, foot, landing, scratch, provider))
                .cost,
            1u);

  const auto bounded =
      tess::build_bounded_weighted_distance_field<StairWorld, DefaultClass, 4>(
          world, landing, scratch, tess::MissingChunkPolicy::TreatAsBlocked,
          provider);
  ASSERT_EQ(bounded.status, tess::PathStatus::Found);
  EXPECT_EQ((tess::weighted_distance_field_path<StairWorld, DefaultClass>(
                 world, foot, landing, scratch, provider))
                .cost,
            1u);
}

TEST(TessPathMovementClass, SparseProviderFieldReportsMissingFootTopology) {
  using StairSchema = tess::FieldSchema<tess::Field<PassableTag, bool>,
                                        tess::Field<CostTag, std::uint32_t>,
                                        tess::Field<StairTag, std::uint8_t>>;
  using StairShape =
      tess::Shape<tess::Extent3{4, 4, 4}, tess::Extent3{4, 4, 2}>;
  using StairWorld = tess::SparseResidentWorld<StairShape, StairSchema>;
  StairWorld world{tess::ResidencyConfig{StairWorld::page_byte_size}};
  world.ensure_resident(tess::ChunkKey{1});
  auto& page = world.chunk(tess::ChunkKey{1});
  std::fill(page.field_span<PassableTag>().begin(),
            page.field_span<PassableTag>().end(), true);
  std::fill(page.field_span<CostTag>().begin(),
            page.field_span<CostTag>().end(), 1u);
  const auto provider = tess::StairTransitions<StairTag>{};
  tess::DistanceFieldScratch scratch;

  const auto result =
      tess::build_weighted_distance_field<StairWorld, DefaultClass>(
          world, tess::Coord3{2, 1, 2}, scratch,
          tess::MissingChunkPolicy::Indeterminate, provider);

  EXPECT_EQ(result.status, tess::PathStatus::Indeterminate);
}

TEST(TessPathMovementClass, ProviderAwareCommitAcceptsPlannedStairEdge) {
  using StairSchema = tess::FieldSchema<
      tess::Field<PassableTag, bool>, tess::Field<CostTag, std::uint32_t>,
      tess::Field<OccupancyTag, bool>, tess::Field<ReservationTag, bool>,
      tess::Field<StairTag, std::uint8_t>>;
  using StairShape =
      tess::Shape<tess::Extent3{4, 4, 2}, tess::Extent3{4, 4, 2}>;
  using StairWorld = tess::AlwaysResidentWorld<StairShape, StairSchema>;
  StairWorld world;
  for (auto& page : world.chunks()) {
    std::fill(page.field_span<PassableTag>().begin(),
              page.field_span<PassableTag>().end(), true);
    std::fill(page.field_span<CostTag>().begin(),
              page.field_span<CostTag>().end(), 1u);
  }
  constexpr auto foot = tess::Coord3{1, 1, 0};
  constexpr auto landing = tess::Coord3{2, 1, 1};
  world.field<StairTag>(foot) =
      static_cast<std::uint8_t>(tess::StairDirection::PositiveX);
  world.field<OccupancyTag>(foot) = true;
  const auto intent = tess::MovementIntent{.from = foot, .to = landing};
  const auto provider = tess::StairTransitions<StairTag>{};

  const auto without =
      tess::validate_movement_intent<StairWorld, DefaultClass, OccupancyTag,
                                     ReservationTag>(world, intent);
  EXPECT_EQ(without.status, tess::MovementStatus::NotAdjacent);

  const auto moved =
      tess::commit_movement_intent<StairWorld, DefaultClass, OccupancyTag,
                                   ReservationTag>(world, intent, 0, provider);
  EXPECT_EQ(moved.status, tess::MovementStatus::Moved);
  EXPECT_FALSE(world.field<OccupancyTag>(foot));
  EXPECT_TRUE(world.field<OccupancyTag>(landing));
}

TEST(TessPathMovementClass, DiagonalRouteCachePreservesScaleAndExactCost) {
  World world;
  fill_open(world, 1);
  tess::RouteCacheScratch cache;
  tess::PathScratch scratch;
  constexpr auto goal = tess::Coord3{3, 3, 0};

  const auto first = tess::cached_astar_path<World, Diagonal>(
      world, tess::PathRequest{tess::Coord3{0, 0, 0}, goal}, scratch, cache);
  const auto hit = tess::cached_astar_path<World, Diagonal>(
      world, tess::PathRequest{tess::Coord3{0, 0, 0}, goal}, scratch, cache);
  const auto suffix = tess::cached_astar_path<World, Diagonal>(
      world, tess::PathRequest{tess::Coord3{1, 1, 0}, goal}, scratch, cache);

  EXPECT_EQ(first.cost, 3u * 181u);
  EXPECT_EQ(first.cost_scale, 128u);
  EXPECT_EQ(hit.cost, first.cost);
  EXPECT_EQ(hit.cost_scale, 128u);
  EXPECT_EQ(suffix.cost, 2u * 181u);
  EXPECT_EQ(suffix.cost_scale, 128u);
  EXPECT_EQ(cache.stats().hits, 1u);
  EXPECT_EQ(cache.stats().suffix_hits, 0u);
}

TEST(TessPathMovementClass, RouteCacheInvalidatesWhenProviderChanges) {
  using StairSchema = tess::FieldSchema<tess::Field<PassableTag, bool>,
                                        tess::Field<CostTag, std::uint32_t>,
                                        tess::Field<StairTag, std::uint8_t>>;
  using StairShape =
      tess::Shape<tess::Extent3{4, 4, 2}, tess::Extent3{4, 4, 2}>;
  using StairWorld = tess::AlwaysResidentWorld<StairShape, StairSchema>;
  StairWorld world;
  for (auto& page : world.chunks()) {
    std::fill(page.field_span<PassableTag>().begin(),
              page.field_span<PassableTag>().end(), true);
    std::fill(page.field_span<CostTag>().begin(),
              page.field_span<CostTag>().end(), 1u);
  }
  constexpr auto foot = tess::Coord3{1, 1, 0};
  constexpr auto landing = tess::Coord3{2, 1, 1};
  world.field<StairTag>(foot) =
      static_cast<std::uint8_t>(tess::StairDirection::PositiveX);
  const auto provider = tess::StairTransitions<StairTag>{};
  const auto request = tess::PathRequest{foot, landing};
  tess::RouteCacheScratch cache;
  tess::PathScratch scratch;

  const auto special = tess::cached_astar_path<StairWorld, PassableTag>(
      world, request, scratch, cache, provider);
  const auto special_hit = tess::cached_astar_path<StairWorld, PassableTag>(
      world, request, scratch, cache, provider);
  const auto regular = tess::cached_astar_path<StairWorld, PassableTag>(
      world, request, scratch, cache);

  EXPECT_EQ(special.cost, 1u);
  EXPECT_EQ(special_hit.cost, 1u);
  EXPECT_EQ(regular.cost, 2u);
  EXPECT_EQ(cache.stats().provider_rebinds, 1u);
}

TEST(TessPathMovementClass, BatchPreservesDiagonalAndProviderModels) {
  World diagonal_world;
  fill_open(diagonal_world, 1);
  const auto diagonal_requests = std::array{
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{3, 3, 0}},
      tess::PathRequest{tess::Coord3{1, 1, 0}, tess::Coord3{3, 3, 0}},
  };
  tess::WeightedPathBatchScratch diagonal_scratch;
  const auto diagonal = tess::weighted_path_batch<World, Diagonal, 4>(
      diagonal_world, diagonal_requests, diagonal_scratch);
  ASSERT_EQ(diagonal.size(), 2u);
  EXPECT_EQ(diagonal[0].cost, 3u * 181u);
  EXPECT_EQ(diagonal[0].cost_scale, 128u);
  EXPECT_EQ(diagonal[1].cost, 2u * 181u);
  EXPECT_EQ(diagonal[1].cost_scale, 128u);

  using StairSchema = tess::FieldSchema<tess::Field<PassableTag, bool>,
                                        tess::Field<CostTag, std::uint32_t>,
                                        tess::Field<StairTag, std::uint8_t>>;
  using StairShape =
      tess::Shape<tess::Extent3{4, 4, 2}, tess::Extent3{4, 4, 2}>;
  using StairWorld = tess::AlwaysResidentWorld<StairShape, StairSchema>;
  StairWorld stair_world;
  for (auto& page : stair_world.chunks()) {
    std::fill(page.field_span<PassableTag>().begin(),
              page.field_span<PassableTag>().end(), true);
    std::fill(page.field_span<CostTag>().begin(),
              page.field_span<CostTag>().end(), 1u);
  }
  constexpr auto foot = tess::Coord3{1, 1, 0};
  constexpr auto landing = tess::Coord3{2, 1, 1};
  stair_world.field<StairTag>(foot) =
      static_cast<std::uint8_t>(tess::StairDirection::PositiveX);
  const auto stair_requests = std::array{
      tess::PathRequest{foot, landing},
      tess::PathRequest{tess::Coord3{0, 1, 0}, landing},
  };
  const auto provider = tess::StairTransitions<StairTag>{};
  tess::WeightedPathBatchScratch stair_scratch;
  const auto stair = tess::weighted_path_batch<StairWorld, DefaultClass, 4>(
      stair_world, stair_requests, stair_scratch, provider);
  ASSERT_EQ(stair.size(), 2u);
  EXPECT_EQ(stair[0].cost, 1u);
  EXPECT_EQ(stair[0].path.size(), 2u);
  EXPECT_EQ(stair[1].cost, 2u);
}

TEST(TessPathMovementClass, RuntimeProcessesProviderAwareUnitAndWeightedPaths) {
  using StairSchema = tess::FieldSchema<tess::Field<PassableTag, bool>,
                                        tess::Field<CostTag, std::uint32_t>,
                                        tess::Field<StairTag, std::uint8_t>>;
  using StairShape =
      tess::Shape<tess::Extent3{4, 4, 2}, tess::Extent3{4, 4, 2}>;
  using StairWorld = tess::AlwaysResidentWorld<StairShape, StairSchema>;
  StairWorld world;
  for (auto& page : world.chunks()) {
    std::fill(page.field_span<PassableTag>().begin(),
              page.field_span<PassableTag>().end(), true);
    std::fill(page.field_span<CostTag>().begin(),
              page.field_span<CostTag>().end(), 1u);
  }
  constexpr auto foot = tess::Coord3{1, 1, 0};
  constexpr auto landing = tess::Coord3{2, 1, 1};
  world.field<StairTag>(foot) =
      static_cast<std::uint8_t>(tess::StairDirection::PositiveX);
  const auto provider = tess::StairTransitions<StairTag>{};
  tess::PathRequestRuntime runtime;
  [[maybe_unused]] const auto ticket =
      runtime.submit(tess::PathRequest{foot, landing});

  const auto unit = runtime.process_unit_cached<StairWorld, PassableTag>(
      world, tess::PathRuntimeCachePolicy{}, nullptr, provider);
  ASSERT_EQ(unit.size(), 1u);
  EXPECT_EQ(unit[0].cost, 1u);

  const auto weighted =
      runtime.process_weighted_batch<StairWorld, DefaultClass, 4>(
          world, tess::PathRuntimeCachePolicy{}, nullptr, provider);
  ASSERT_EQ(weighted.size(), 1u);
  EXPECT_EQ(weighted[0].cost, 1u);
}

TEST(TessPathMovementClass, ProviderAwareAgentPlansAndCommitsSameStair) {
  using StairSchema = tess::FieldSchema<
      tess::Field<PassableTag, bool>, tess::Field<CostTag, std::uint32_t>,
      tess::Field<OccupancyTag, bool>, tess::Field<ReservationTag, bool>,
      tess::Field<StairTag, std::uint8_t>>;
  using StairShape =
      tess::Shape<tess::Extent3{4, 4, 2}, tess::Extent3{4, 4, 2}>;
  using StairWorld = tess::AlwaysResidentWorld<StairShape, StairSchema>;
  StairWorld world;
  for (auto& page : world.chunks()) {
    std::fill(page.field_span<PassableTag>().begin(),
              page.field_span<PassableTag>().end(), true);
    std::fill(page.field_span<CostTag>().begin(),
              page.field_span<CostTag>().end(), 1u);
  }
  constexpr auto foot = tess::Coord3{1, 1, 0};
  constexpr auto landing = tess::Coord3{2, 1, 1};
  world.field<StairTag>(foot) =
      static_cast<std::uint8_t>(tess::StairDirection::PositiveX);
  world.field<OccupancyTag>(foot) = true;
  const auto provider = tess::StairTransitions<StairTag>{};
  tess::PathAgentState agent;
  agent.position = foot;
  tess::set_path_agent_goal(agent, landing);
  auto agents = std::span<tess::PathAgentState>{&agent, 1};
  tess::PathAgentTickState state;
  tess::PathRequestRuntime runtime;

  const auto tick =
      tess::tick_unit_path_agents_with_movement<StairWorld, DefaultClass,
                                                OccupancyTag, ReservationTag>(
          state, world, agents, runtime, tess::PathAgentTickOptions{}, 0,
          nullptr, provider);
  EXPECT_TRUE(tick.processed_paths);
  EXPECT_EQ(tick.pathing.found, 1u);
  EXPECT_EQ(tick.movement.advanced, 1u);
  EXPECT_EQ(tick.movement.arrived, 1u);
  EXPECT_EQ(agent.position, landing);
  EXPECT_FALSE(world.field<OccupancyTag>(foot));
  EXPECT_TRUE(world.field<OccupancyTag>(landing));
}

TEST(TessPathMovementClass, LegacyWeightedMatchesTagPairWeightedSearch) {
  World world;
  fill_open(world, 2);
  carve_serpentine(world);
  // Expensive band plus a zero-cost (impassable-to-weighted) tile exercise
  // the normalize_cost contract inside the class leaf. The zero-cost tile
  // sits on the goal row but off the forced serpentine corridor.
  world.field<CostTag>(tess::Coord3{3, 0, 0}) = 9;
  world.field<CostTag>(tess::Coord3{4, 0, 0}) = 9;
  world.field<CostTag>(tess::Coord3{3, 6, 0}) = 0;

  tess::PathScratch tag_scratch;
  tess::PathScratch class_scratch;
  const auto request =
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{7, 6, 0}};

  const auto by_pair = tess::weighted_astar_path<World, PassableTag, CostTag>(
      world, request, tag_scratch);
  const auto by_class =
      tess::weighted_astar_path<World,
                                mv::LegacyWeighted<PassableTag, CostTag>>(
          world, request, class_scratch);

  ASSERT_EQ(by_pair.status, tess::PathStatus::Found);
  expect_same_result(by_pair, by_class);
}

TEST(TessPathMovementClass, LegacyWeightedMatchesTagPairDistanceField) {
  World world;
  fill_open(world, 2);
  carve_serpentine(world);
  world.field<CostTag>(tess::Coord3{2, 4, 0}) = 7;

  const auto goal = tess::Coord3{7, 6, 0};
  tess::DistanceFieldScratch tag_scratch;
  tess::DistanceFieldScratch class_scratch;

  const auto tag_field =
      tess::build_weighted_distance_field<World, PassableTag, CostTag>(
          world, goal, tag_scratch);
  const auto class_field = tess::build_weighted_distance_field<
      World, mv::LegacyWeighted<PassableTag, CostTag>>(world, goal,
                                                       class_scratch);
  ASSERT_EQ(tag_field.status, tess::PathStatus::Found);
  EXPECT_EQ(tag_field.status, class_field.status);
  EXPECT_EQ(tag_field.expanded_nodes, class_field.expanded_nodes);
  EXPECT_EQ(tag_field.reached_nodes, class_field.reached_nodes);

  const auto start = tess::Coord3{0, 0, 0};
  const auto by_pair =
      tess::weighted_distance_field_path<World, PassableTag, CostTag>(
          world, start, goal, tag_scratch);
  const auto by_class = tess::weighted_distance_field_path<
      World, mv::LegacyWeighted<PassableTag, CostTag>>(world, start, goal,
                                                       class_scratch);
  ASSERT_EQ(by_pair.status, tess::PathStatus::Found);
  expect_same_result(by_pair, by_class);
}

TEST(TessPathMovementClass, WalkerRoutesAroundConstructionBuilderThroughIt) {
  World world;
  fill_open(world, 1);
  // A full construction wall across x=3 except a passable gap at y=7: the
  // Builder cuts straight through the wall, the Walker detours to the gap.
  for (std::int64_t y = 0; y < 7; ++y) {
    world.field<PassableTag>(tess::Coord3{3, y, 0}) = false;
    world.field<ConstructionTag>(tess::Coord3{3, y, 0}) = 1;
  }

  tess::PathScratch scratch;
  const auto request =
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{7, 0, 0}};

  const auto walker =
      tess::weighted_astar_path<World, Walker>(world, request, scratch);
  ASSERT_EQ(walker.status, tess::PathStatus::Found);
  const auto walker_through_wall =
      std::any_of(walker.path.begin(), walker.path.end(),
                  [](tess::Coord3 c) { return c.x == 3 && c.y < 7; });
  EXPECT_FALSE(walker_through_wall);
  EXPECT_EQ(walker.cost, 21u);  // detour via y=7: 7 across + 14 vertical

  tess::PathScratch builder_scratch;
  const auto builder = tess::weighted_astar_path<World, Builder>(
      world, request, builder_scratch);
  ASSERT_EQ(builder.status, tess::PathStatus::Found);
  const auto builder_through_wall =
      std::any_of(builder.path.begin(), builder.path.end(),
                  [](tess::Coord3 c) { return c.x == 3 && c.y < 7; });
  EXPECT_TRUE(builder_through_wall);
  EXPECT_EQ(builder.cost, 9u);  // 6 unit steps + the build price 3
}

TEST(TessPathMovementClass, UnitSearchAcceptsClassesForPassabilityOnly) {
  World world;
  fill_open(world, 1);
  world.field<PassableTag>(tess::Coord3{3, 0, 0}) = false;
  world.field<ConstructionTag>(tess::Coord3{3, 0, 0}) = 1;
  for (std::int64_t y = 1; y < 8; ++y) {
    world.field<PassableTag>(tess::Coord3{3, y, 0}) = false;
  }

  tess::PathScratch scratch;
  const auto request =
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{7, 0, 0}};

  // The wall is only breachable through the construction tile: unit A* over
  // the Builder class finds it, over the Walker class there is no route.
  const auto builder =
      tess::astar_path<World, Builder>(world, request, scratch);
  EXPECT_EQ(builder.status, tess::PathStatus::Found);
  const auto walker = tess::astar_path<World, Walker>(world, request, scratch);
  EXPECT_EQ(walker.status, tess::PathStatus::NoPath);
}

TEST(TessPathMovementClass, ClassSearchHonorsMissingChunksOnSparseWorlds) {
  using TwoChunk =
      tess::Shape<tess::Extent3{64, 32, 1}, tess::Extent3{32, 32, 1}>;
  using Sparse = tess::SparseResidentWorld<TwoChunk, Schema>;
  Sparse world{tess::ResidencyConfig{2 * Sparse::page_byte_size}};

  world.ensure_resident(tess::ChunkKey{0});
  auto& page = world.chunk(tess::ChunkKey{0});
  for (std::uint64_t i = 0; i < Sparse::local_tile_count; ++i) {
    const auto tile = tess::LocalTileId{i};
    page.template field<PassableTag>(tile) = true;
    page.template field<CostTag>(tile) = 1u;
  }

  tess::PathScratch scratch;
  // Goal in the non-resident chunk: blocked under the default policy, never a
  // wrong NoPath under Indeterminate.
  const auto request =
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{40, 0, 0}};
  const auto blocked =
      tess::weighted_astar_path<Sparse,
                                mv::LegacyWeighted<PassableTag, CostTag>>(
          world, request, scratch);
  EXPECT_EQ(blocked.status, tess::PathStatus::InvalidGoal);
  const auto indeterminate =
      tess::weighted_astar_path<Sparse,
                                mv::LegacyWeighted<PassableTag, CostTag>>(
          world, request, scratch, tess::MissingChunkPolicy::Indeterminate);
  EXPECT_EQ(indeterminate.status, tess::PathStatus::Indeterminate);
}

// S5.5: plan == commit. Every step weighted A* accepts for a class passes
// validate_movement_intent for that SAME class, and the block statuses are
// per class on both endpoints.
TEST(TessPathMovementClass, CommitValidationAgreesWithThePlannedClass) {
  World world;
  fill_open(world, 1);
  for (std::int64_t y = 0; y < 7; ++y) {
    world.field<PassableTag>(tess::Coord3{3, y, 0}) = false;
    world.field<ConstructionTag>(tess::Coord3{3, y, 0}) = 1;
  }

  const auto request =
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{7, 0, 0}};
  const auto validate_every_step = [&](const tess::PathResult& result,
                                       auto class_probe) {
    using Class = decltype(class_probe);
    ASSERT_EQ(result.status, tess::PathStatus::Found);
    for (std::size_t i = 1; i < result.path.size(); ++i) {
      const auto step =
          tess::validate_movement_intent<World, Class, OccupancyTag,
                                         ReservationTag>(
              world, tess::MovementIntent{result.path[i - 1], result.path[i]});
      EXPECT_EQ(step.status, tess::MovementStatus::Moved)
          << "step " << i << " (" << result.path[i].x << "," << result.path[i].y
          << ")";
    }
  };

  tess::PathScratch walker_scratch;
  const auto walker_path =
      tess::weighted_astar_path<World, Walker>(world, request, walker_scratch);
  validate_every_step(walker_path, Walker{});

  tess::PathScratch builder_scratch;
  const auto builder_path = tess::weighted_astar_path<World, Builder>(
      world, request, builder_scratch);
  validate_every_step(builder_path, Builder{});

  // Block statuses are per class on both endpoints: the Walker may neither
  // enter nor leave a construction site, the Builder may do both.
  const auto site = tess::Coord3{3, 0, 0};
  const auto open = tess::Coord3{4, 0, 0};
  const auto into_site = tess::MovementIntent{open, site};
  const auto out_of_site = tess::MovementIntent{site, open};
  const auto walker_in =
      tess::validate_movement_intent<World, Walker, OccupancyTag,
                                     ReservationTag>(world, into_site);
  EXPECT_EQ(walker_in.status, tess::MovementStatus::BlockedTo);
  const auto walker_out =
      tess::validate_movement_intent<World, Walker, OccupancyTag,
                                     ReservationTag>(world, out_of_site);
  EXPECT_EQ(walker_out.status, tess::MovementStatus::BlockedFrom);
  const auto builder_in =
      tess::validate_movement_intent<World, Builder, OccupancyTag,
                                     ReservationTag>(world, into_site);
  EXPECT_EQ(builder_in.status, tess::MovementStatus::Moved);
  const auto builder_out =
      tess::validate_movement_intent<World, Builder, OccupancyTag,
                                     ReservationTag>(world, out_of_site);
  EXPECT_EQ(builder_out.status, tess::MovementStatus::Moved);
}

TEST(TessPathMovementClass, DiagonalCommitUsesResolvedClearance) {
  World world;
  fill_open(world, 1);
  constexpr auto from = tess::Coord3{2, 2, 0};
  constexpr auto to = tess::Coord3{3, 3, 0};
  world.field<OccupancyTag>(from) = true;
  world.field<PassableTag>(tess::Coord3{3, 2, 0}) = false;

  const auto blocked =
      tess::validate_movement_intent<World, Diagonal, OccupancyTag,
                                     ReservationTag>(
          world, tess::MovementIntent{.from = from, .to = to});
  EXPECT_EQ(blocked.status, tess::MovementStatus::BlockedTo);

  world.field<PassableTag>(tess::Coord3{3, 2, 0}) = true;
  const auto moved = tess::commit_movement_intent<World, Diagonal, OccupancyTag,
                                                  ReservationTag>(
      world, tess::MovementIntent{.from = from, .to = to});
  EXPECT_EQ(moved.status, tess::MovementStatus::Moved);
  EXPECT_FALSE(world.field<OccupancyTag>(from));
  EXPECT_TRUE(world.field<OccupancyTag>(to));
}

// S5.6: the unit route cache keys on (start, goal) only, so a runtime reused
// across classes must clear rather than serve another class's cached route.
TEST(TessPathMovementClass, RuntimeReboundToAnotherClassNeverServesStaleRoute) {
  World world;
  fill_open(world, 1);
  for (std::int64_t y = 0; y < 7; ++y) {
    world.field<PassableTag>(tess::Coord3{3, y, 0}) = false;
    world.field<ConstructionTag>(tess::Coord3{3, y, 0}) = 1;
  }

  tess::PathRequestRuntime runtime;
  runtime.reserve_requests(1);
  runtime.reserve_search_nodes(64);
  runtime.reserve_path_nodes(64);
  runtime.reserve_unit_routes(2);
  const auto request =
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{7, 0, 0}};

  // Walker detours through the y=7 gap: 21 unit steps.
  (void)runtime.submit(request);
  auto results = runtime.process_unit_cached<World, Walker>(world);
  ASSERT_EQ(results.size(), 1u);
  ASSERT_EQ(results[0].status, tess::PathStatus::Found);
  EXPECT_EQ(results[0].cost, 21u);
  EXPECT_EQ(runtime.stats().class_cache_invalidations, 0u);

  // Same (start, goal), same world version, different class: the rebind
  // clears the unit caches and the Builder gets ITS 7-step route, not the
  // Walker's cached 21-step detour.
  runtime.clear_requests();
  (void)runtime.submit(request);
  results = runtime.process_unit_cached<World, Builder>(world);
  ASSERT_EQ(results.size(), 1u);
  ASSERT_EQ(results[0].status, tess::PathStatus::Found);
  EXPECT_EQ(results[0].cost, 7u);
  EXPECT_EQ(runtime.stats().class_cache_invalidations, 1u);

  // Rebinding back also invalidates; staying on one class does not.
  runtime.clear_requests();
  (void)runtime.submit(request);
  results = runtime.process_unit_cached<World, Builder>(world);
  EXPECT_EQ(runtime.stats().class_cache_invalidations, 1u);
  EXPECT_EQ(results[0].cost, 7u);
}

// S5.6: one movement class drives the whole tick -- pathing, precheck, and
// commit -- so two classes over one world route and move differently.
TEST(TessPathMovementClass, WeightedClassTicksRouteAndCommitPerClass) {
  // Each class gets its own world: an arrived agent's occupancy stays on its
  // goal tile, which would (correctly) block a second run sharing the world.
  const auto run_to_goal =
      [](auto class_probe) -> std::pair<std::size_t, std::size_t> {
    using Class = decltype(class_probe);
    World world;
    fill_open(world, 1);
    for (std::int64_t y = 0; y < 7; ++y) {
      world.field<PassableTag>(tess::Coord3{3, y, 0}) = false;
      world.field<ConstructionTag>(tess::Coord3{3, y, 0}) = 1;
    }
    tess::PathAgentTickState state;
    tess::PathRequestRuntime runtime;
    runtime.reserve_requests(1);
    runtime.reserve_search_nodes(64);
    runtime.reserve_path_nodes(64);
    runtime.reserve_unit_routes(1);
    tess::PathAgentState agent;
    agent.position = tess::Coord3{0, 0, 0};
    tess::set_path_agent_goal(state, agent, tess::Coord3{7, 0, 0});
    auto agents = std::span<tess::PathAgentState>{&agent, 1};

    std::size_t ticks = 0;
    std::size_t blocked = 0;
    while (agent.has_goal && ticks < 64) {
      const auto stats = tess::tick_weighted_path_agents_with_movement<
          World, Class, 64, OccupancyTag, ReservationTag>(state, world, agents,
                                                          runtime, {});
      blocked += stats.movement.movement_failures.blocked;
      ++ticks;
    }
    EXPECT_FALSE(agent.has_goal) << "agent never arrived";
    EXPECT_EQ(agent.position, (tess::Coord3{7, 0, 0}));
    return {ticks, blocked};
  };

  // Builder crosses the wall (7 steps); Walker detours (21 steps). Neither
  // ever has a movement step rejected: the commit validates with the SAME
  // class the plan used.
  const auto [builder_ticks, builder_blocked] = run_to_goal(Builder{});
  const auto [walker_ticks, walker_blocked] = run_to_goal(Walker{});
  EXPECT_EQ(builder_blocked, 0u);
  EXPECT_EQ(walker_blocked, 0u);
  EXPECT_LT(builder_ticks, walker_ticks);
}

// Regression (pre-merge review): a policy-triggered cache clear inside a
// process call zeroes the class binding; binding must happen AFTER that
// clear, or the call would refill the caches under an unbound identity a
// later class could silently reuse.
TEST(TessPathMovementClass, PolicyClearNeverUnbindsTheClassGuard) {
  World world;
  fill_open(world, 1);
  for (std::int64_t y = 0; y < 7; ++y) {
    world.field<PassableTag>(tess::Coord3{3, y, 0}) = false;
    world.field<ConstructionTag>(tess::Coord3{3, y, 0}) = 1;
  }

  tess::PathRequestRuntime runtime;
  runtime.reserve_requests(1);
  const auto policy = tess::PathRuntimeCachePolicy{
      .clear_every_world_change = 1,
  };
  const auto request =
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{7, 0, 0}};

  (void)runtime.submit(request);
  (void)runtime.process_unit_cached<World, Walker>(world, policy);

  // A world edit arms the policy clear inside the NEXT process call.
  world.mark_dirty(tess::ChunkKey{0}, 1u,
                   tess::Box3{tess::Coord3{0, 0, 0}, tess::Extent3{1, 1, 1}});
  runtime.clear_requests();
  (void)runtime.submit(request);
  auto results = runtime.process_unit_cached<World, Walker>(world, policy);
  ASSERT_EQ(results[0].status, tess::PathStatus::Found);
  EXPECT_EQ(results[0].cost, 21u);  // the Walker detour, freshly cached

  // Same (start, goal), other class: the Builder must get ITS route, never
  // the Walker's just-cached detour.
  runtime.clear_requests();
  (void)runtime.submit(request);
  results = runtime.process_unit_cached<World, Builder>(world, policy);
  ASSERT_EQ(results[0].status, tess::PathStatus::Found);
  EXPECT_EQ(results[0].cost, 7u);
  EXPECT_GE(runtime.stats().class_cache_invalidations, 1u);
}

// Codex review: DIRECT cached_astar_path callers get the same class guard
// the runtime has -- the route cache binds itself to the call's class and a
// rebind drops the entries instead of serving another class's route.
TEST(TessPathMovementClass, DirectRouteCacheNeverServesAnotherClass) {
  World world;
  fill_open(world, 1);
  for (std::int64_t y = 0; y < 7; ++y) {
    world.field<PassableTag>(tess::Coord3{3, y, 0}) = false;
    world.field<ConstructionTag>(tess::Coord3{3, y, 0}) = 1;
  }

  tess::PathScratch scratch;
  tess::RouteCacheScratch cache;
  const auto request =
      tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{7, 0, 0}};

  const auto walker =
      tess::cached_astar_path<World, Walker>(world, request, scratch, cache);
  ASSERT_EQ(walker.status, tess::PathStatus::Found);
  EXPECT_EQ(walker.cost, 21u);
  EXPECT_EQ(cache.stats().class_rebinds, 0u);

  const auto builder =
      tess::cached_astar_path<World, Builder>(world, request, scratch, cache);
  ASSERT_EQ(builder.status, tess::PathStatus::Found);
  EXPECT_EQ(builder.cost, 7u);
  EXPECT_EQ(cache.stats().class_rebinds, 1u);

  // Staying on one class serves the cache normally.
  const auto again =
      tess::cached_astar_path<World, Builder>(world, request, scratch, cache);
  EXPECT_EQ(again.cost, 7u);
  EXPECT_EQ(cache.stats().class_rebinds, 1u);
  EXPECT_GE(cache.stats().hits, 1u);
}

}  // namespace
