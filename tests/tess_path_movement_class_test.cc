#include <gtest/gtest.h>
#include <tess/tess.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>

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

}  // namespace
