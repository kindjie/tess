#include <gtest/gtest.h>
#include <tess/navigation/grid.h>
#include <tess/navigation/query.h>
#include <tess/storage/sparse_world.h>
#include <tess/storage/world.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

namespace {

struct Open {};
struct Cost {};
using Schema = tess::FieldSchema<tess::Field<Open, bool>,
                                 tess::Field<Cost, std::uint64_t>>;
using Shape = tess::Shape<tess::Extent3{8, 8, 1}, tess::Extent3{4, 4, 1}>;
using World = tess::AlwaysResidentWorld<Shape, Schema>;
using Movement = tess::movement::MovementClass<tess::movement::Field<Open>,
                                               tess::movement::FieldCost<Cost>>;
using Diagonal = tess::movement::MovementClass<tess::movement::Field<Open>,
                                               tess::movement::FieldCost<Cost>,
                                               tess::movement::DiagonalSteps<>>;

auto open_world(std::uint64_t cost = 1) -> std::shared_ptr<World> {
  auto world = std::make_shared<World>();
  world->fill_field<Open>(true);
  world->fill_field<Cost>(cost);
  return world;
}

template <typename Grid>
auto find_edge(const Grid& grid, tess::Coord3 from, tess::Coord3 to,
               bool reverse = false) -> tess::NavigationLocalEdge {
  const auto from_key = grid.coordinate_location(from).node;
  const auto to_key = grid.coordinate_location(to).node;
  const auto node = reverse ? to_key : from_key;
  for (std::size_t i = 0; i < grid.degree(node, reverse); ++i) {
    const auto edge = grid.edge(node, i, reverse);
    if (edge.from == from_key && edge.to == to_key) return edge;
  }
  ADD_FAILURE() << "regular candidate missing";
  return {.availability = tess::NavigationAvailability::Invalid};
}

TEST(TessNavigationGrid, ReverseKeepsForwardEndpointsCostAndIdentity) {
  auto world = open_world(2);
  constexpr tess::Coord3 from{3, 3, 0};
  constexpr tess::Coord3 to{4, 3, 0};
  world->field<Cost>(to) = 7;
  tess::NavigationGrid<World, Movement> grid{std::move(world), 3};
  const auto forward = find_edge(grid, from, to);
  const auto reverse = find_edge(grid, from, to, true);
  EXPECT_EQ(forward.availability, tess::NavigationAvailability::Legal);
  EXPECT_EQ(forward.cost, 21u);
  EXPECT_EQ(reverse.from, forward.from);
  EXPECT_EQ(reverse.to, forward.to);
  EXPECT_EQ(reverse.cost, forward.cost);
  EXPECT_EQ(reverse.id, forward.id);
  EXPECT_EQ(find_edge(grid, to, from).cost, 6u);
  EXPECT_TRUE(grid.complete());
}

TEST(TessNavigationGrid, DiagonalClearanceAndCommonUnitScaling) {
  auto world = open_world(2);
  world->field<Open>({4, 3, 0}) = false;
  tess::NavigationGrid<World, Diagonal> grid{std::move(world), 5};
  EXPECT_EQ(find_edge(grid, {3, 3, 0}, {4, 4, 0}).availability,
            tess::NavigationAvailability::Blocked);
  const auto clear = find_edge(grid, {2, 2, 0}, {3, 3, 0});
  EXPECT_EQ(clear.availability, tess::NavigationAvailability::Legal);
  EXPECT_EQ(clear.cost, 2u * 181u * 5u);
  EXPECT_EQ(find_edge(grid, {2, 2, 0}, {3, 2, 0}).cost, 2u * 128u * 5u);
}

TEST(TessNavigationGrid, SparseMembershipDoesNotPromiseResidentTopology) {
  using Sparse = tess::SparseResidentWorld<Shape, Schema>;
  auto world =
      std::make_shared<Sparse>(tess::ResidencyConfig{Sparse::page_byte_size});
  world->ensure_resident(tess::ChunkKey{0});
  auto open = world->chunk(tess::ChunkKey{0}).field_span<Open>();
  std::fill(open.begin(), open.end(), true);
  auto costs = world->chunk(tess::ChunkKey{0}).field_span<Cost>();
  std::fill(costs.begin(), costs.end(), 1);
  tess::NavigationGrid<Sparse, Movement> grid{std::move(world), 1};
  const auto absent = grid.coordinate_location({4, 2, 0});
  EXPECT_TRUE(grid.contains(absent.node));
  EXPECT_EQ(grid.availability(absent.node),
            tess::NavigationAvailability::Unavailable);
  EXPECT_EQ(find_edge(grid, {3, 2, 0}, {4, 2, 0}).availability,
            tess::NavigationAvailability::Unavailable);
  EXPECT_EQ(find_edge(grid, {3, 2, 0}, {4, 2, 0}, true).availability,
            tess::NavigationAvailability::Unavailable);
  EXPECT_FALSE(grid.complete());
}

struct WallVeto {
  tess::Coord3 from{};
  tess::Coord3 to{};
  auto operator()(const World&, tess::Coord3 a, tess::Coord3 b) const noexcept
      -> bool {
    return a != from || b != to;
  }
};

struct ThrowingVeto {
  auto operator()(const World&, tess::Coord3, tess::Coord3) const -> bool {
    throw std::runtime_error{"edge veto failed"};
  }
};

TEST(TessNavigationGrid, HostVetoExceptionsPropagate) {
  tess::NavigationGrid<World, Movement, ThrowingVeto> grid{open_world(), 1};
  EXPECT_THROW(
      (void)grid.edge(grid.coordinate_location({2, 2, 0}).node, 0, false),
      std::runtime_error);
}

TEST(TessNavigationGrid, OwnedPhysicalVetoIsDirectedAndSharedByReverse) {
  auto world = open_world();
  WallVeto filter{{3, 3, 0}, {4, 3, 0}};
  tess::NavigationGrid<World, Movement, WallVeto> grid{std::move(world), 1,
                                                       filter};
  filter.to = {2, 3, 0};
  EXPECT_EQ(find_edge(grid, {3, 3, 0}, {4, 3, 0}).availability,
            tess::NavigationAvailability::Blocked);
  EXPECT_EQ(find_edge(grid, {3, 3, 0}, {4, 3, 0}, true).availability,
            tess::NavigationAvailability::Blocked);
  EXPECT_EQ(find_edge(grid, {4, 3, 0}, {3, 3, 0}).availability,
            tess::NavigationAvailability::Legal);
}

TEST(TessNavigationGrid, CompactOverflowIsNotAcceptedAsSaturatedCost) {
  const auto max32 = std::numeric_limits<std::uint32_t>::max();
  tess::NavigationGrid<World, Movement> exact{open_world(max32 - 1u), 1};
  EXPECT_EQ(find_edge(exact, {2, 2, 0}, {3, 2, 0}).cost, max32 - 1u);
  for (const auto value : {std::uint64_t{max32}, std::uint64_t{max32} + 99u}) {
    tess::NavigationGrid<World, Movement> grid{open_world(value), 1};
    EXPECT_EQ(find_edge(grid, {2, 2, 0}, {3, 2, 0}).availability,
              tess::NavigationAvailability::Overflow);
  }
  tess::NavigationGrid<World, Diagonal> diagonal{open_world(max32 / 181u + 1u),
                                                 1};
  EXPECT_EQ(find_edge(diagonal, {2, 2, 0}, {3, 3, 0}).availability,
            tess::NavigationAvailability::Overflow);
}

TEST(TessNavigationGrid, ScalingRejectsReservedInfinityAndMultiplicationWrap) {
  constexpr auto max64 = std::numeric_limits<std::uint64_t>::max();
  tess::NavigationGrid<World, Movement> largest{open_world(), max64 - 1u};
  EXPECT_EQ(find_edge(largest, {2, 2, 0}, {3, 2, 0}).cost, max64 - 1u);
  tess::NavigationGrid<World, Movement> infinity{open_world(), max64};
  EXPECT_EQ(find_edge(infinity, {2, 2, 0}, {3, 2, 0}).availability,
            tess::NavigationAvailability::Overflow);
  tess::NavigationGrid<World, Movement> wrapping{open_world(3), max64 / 2u};
  EXPECT_EQ(find_edge(wrapping, {2, 2, 0}, {3, 2, 0}).availability,
            tess::NavigationAvailability::Overflow);
}

TEST(TessNavigationGrid, RetainsWorldAndRejectsInvalidInput) {
  auto world = open_world();
  std::weak_ptr<const World> weak = world;
  tess::NavigationGrid<World, Movement> grid{std::move(world), 1};
  EXPECT_FALSE(weak.expired());
  EXPECT_EQ(grid.coordinate_location({-1, 0, 0}).domain, 0u);
  EXPECT_FALSE(grid.contains(std::numeric_limits<std::uint64_t>::max()));
  EXPECT_EQ(grid.degree(std::numeric_limits<std::uint64_t>::max(), false), 0u);
  EXPECT_EQ(grid.edge(0, 99, false).availability,
            tess::NavigationAvailability::Invalid);
  tess::NavigationGrid<World, Movement> empty{nullptr, 1};
  EXPECT_FALSE(empty.contains(0));
  EXPECT_FALSE(empty.valid());
  tess::NavigationGrid<World, Movement> zero_scale{open_world(), 0};
  EXPECT_EQ(zero_scale.availability(0), tess::NavigationAvailability::Invalid);
  EXPECT_FALSE(zero_scale.valid());
  static_assert(noexcept(grid.contains(0)));
  static_assert(noexcept(grid.coordinate_location({0, 0, 0})));
}

TEST(TessNavigationGrid, FullSparseLeaseCanProveCompleteness) {
  using Sparse = tess::SparseResidentWorld<Shape, Schema>;
  auto world = std::make_shared<Sparse>(
      tess::ResidencyConfig{Sparse::chunk_count * Sparse::page_byte_size});
  for (std::uint64_t key = 0; key < Sparse::chunk_count; ++key)
    world->ensure_resident(tess::ChunkKey{key});
  tess::NavigationGrid<Sparse, Movement> grid{std::move(world), 1};
  EXPECT_TRUE(grid.complete());
}

TEST(TessNavigationGrid, RegularCursorBoundIncludesVolumeAndAxialLattices) {
  using VolumeShape =
      tess::Shape<tess::Extent3{4, 4, 4}, tess::Extent3{4, 4, 4}>;
  using Volume = tess::AlwaysResidentWorld<VolumeShape, Schema>;
  auto volume = std::make_shared<Volume>();
  volume->fill_field<Open>(true);
  volume->fill_field<Cost>(1);
  tess::NavigationGrid<Volume, Movement> grid{std::move(volume), 1};
  const auto center = grid.coordinate_location({1, 1, 1}).node;
  EXPECT_EQ(grid.degree(center, false), 6u);
  EXPECT_EQ(grid.degree(center, true), 6u);
  for (std::size_t cursor = 0; cursor < grid.degree(center, false); ++cursor) {
    const auto forward = grid.edge(center, cursor, false);
    ASSERT_EQ(forward.availability, tess::NavigationAvailability::Legal);
    bool matched = false;
    for (std::size_t i = 0; i < grid.degree(forward.to, true); ++i) {
      const auto reverse = grid.edge(forward.to, i, true);
      if (reverse.from == forward.from && reverse.to == forward.to) {
        EXPECT_EQ(reverse.id, forward.id);
        EXPECT_EQ(reverse.cost, forward.cost);
        matched = true;
      }
    }
    EXPECT_TRUE(matched);
  }
  using HexShape = tess::Shape<tess::Extent3{4, 4, 1}, tess::Extent3{4, 4, 1},
                               tess::lattice::HexAxial>;
  using HexWorld = tess::AlwaysResidentWorld<HexShape, Schema>;
  auto hex = std::make_shared<HexWorld>();
  hex->fill_field<Open>(true);
  hex->fill_field<Cost>(1);
  tess::NavigationGrid<HexWorld, Movement> axial{std::move(hex), 1};
  EXPECT_EQ(axial.degree(axial.coordinate_location({1, 1, 0}).node, false), 6u);
}

TEST(TessNavigationGrid, MixedSchemasComposeGridGraphGridInBothDirections) {
  struct Enabled {};
  using OtherSchema = tess::FieldSchema<tess::Field<Enabled, bool>>;
  using OtherShape =
      tess::Shape<tess::Extent3{4, 1, 1}, tess::Extent3{2, 1, 1}>;
  using OtherWorld = tess::AlwaysResidentWorld<OtherShape, OtherSchema>;
  using OtherMovement = tess::movement::UnitCostFieldMovement<Enabled>;
  auto first =
      std::make_shared<tess::NavigationGrid<World, Movement>>(open_world(2), 3);
  auto other = std::make_shared<OtherWorld>();
  other->fill_field<Enabled>(true);
  auto second =
      std::make_shared<tess::NavigationGrid<OtherWorld, OtherMovement>>(
          std::move(other), 5);
  auto graph = std::make_shared<tess::NavigationGraph>(
      2, std::vector<tess::NavigationLocalEdge>{{0, 1, 1, 7}, {1, 0, 2, 11}});
  const auto a = first->coordinate_location({2, 2, 0});
  const auto b = first->coordinate_location({3, 2, 0});
  const auto c = second->coordinate_location({0, 0, 0});
  const auto d = second->coordinate_location({3, 0, 0});
  auto snapshot = std::make_shared<tess::NavigationSnapshot>(
      std::vector<std::shared_ptr<const tess::NavigationDomain>>{first, graph,
                                                                 second},
      std::vector<tess::NavigationConnection>{
          {b, graph->location(0), 1, 0, 101},
          {graph->location(1), c, 2, 0, 102},
          {c, graph->location(1), 3, 0, 103},
          {graph->location(0), b, 4, 0, 104}});
  ASSERT_TRUE(snapshot->valid());
  const auto forward = tess::navigation_route(snapshot, a, d);
  ASSERT_NE(forward, nullptr);
  EXPECT_EQ(forward->outcome, tess::NavigationOutcome::Found);
  EXPECT_EQ(forward->proof, tess::NavigationProof::ExactSnapshot);
  EXPECT_EQ(forward->cost, 6u + 7u + 3u * 5u);
  ASSERT_EQ(forward->route.size(), 7u);
  EXPECT_EQ(forward->route[1].key, 101u);
  EXPECT_EQ(forward->route[3].key, 102u);
  const auto reverse = tess::navigation_route(snapshot, d, a);
  ASSERT_NE(reverse, nullptr);
  EXPECT_EQ(reverse->outcome, tess::NavigationOutcome::Found);
  EXPECT_EQ(reverse->cost, 3u * 5u + 11u + 6u);
  tess::NavigationQuery guidance{snapshot, d};
  while (guidance.state() == tess::AsyncResultState::Pending) {
    EXPECT_EQ(guidance.advance({1}).items_done, 1u);
  }
  ASSERT_NE(guidance.result(), nullptr);
  const auto* value = guidance.result()->guidance_at(a);
  ASSERT_NE(value, nullptr);
  EXPECT_EQ(value->cost, forward->cost);
  EXPECT_EQ(value->next.from, forward->route.front().from);
  EXPECT_EQ(value->next.to, forward->route.front().to);
}

TEST(TessNavigationGrid, UnknownSparseQueryDoesNotProveNoRoute) {
  using Sparse = tess::SparseResidentWorld<Shape, Schema>;
  auto world =
      std::make_shared<Sparse>(tess::ResidencyConfig{Sparse::page_byte_size});
  world->ensure_resident(tess::ChunkKey{0});
  auto open = world->chunk(tess::ChunkKey{0}).field_span<Open>();
  std::fill(open.begin(), open.end(), true);
  auto costs = world->chunk(tess::ChunkKey{0}).field_span<Cost>();
  std::fill(costs.begin(), costs.end(), 1);
  auto grid = std::make_shared<tess::NavigationGrid<Sparse, Movement>>(
      std::move(world), 1);
  auto snapshot = std::make_shared<tess::NavigationSnapshot>(
      std::vector<std::shared_ptr<const tess::NavigationDomain>>{grid});
  const auto uncertain =
      tess::navigation_route(snapshot, grid->coordinate_location({3, 2, 0}),
                             grid->coordinate_location({4, 2, 0}));
  ASSERT_NE(uncertain, nullptr);
  EXPECT_EQ(uncertain->outcome, tess::NavigationOutcome::Indeterminate);
  const auto feasible =
      tess::navigation_route(snapshot, grid->coordinate_location({1, 1, 0}),
                             grid->coordinate_location({2, 1, 0}));
  ASSERT_NE(feasible, nullptr);
  EXPECT_EQ(feasible->outcome, tess::NavigationOutcome::Found);
  EXPECT_EQ(feasible->proof, tess::NavigationProof::FeasibleOnly);
}

TEST(TessNavigationGrid, SparseDiagonalRequiresOnlyDecisionRelevantClearance) {
  using Sparse = tess::SparseResidentWorld<Shape, Schema>;
  auto world = std::make_shared<Sparse>(
      tess::ResidencyConfig{3 * Sparse::page_byte_size});
  // SW, NW and NE are resident; SE clearance is absent at the chunk corner.
  for (const auto key : {0u, 2u, 3u}) {
    world->ensure_resident(tess::ChunkKey{key});
    auto open = world->chunk(tess::ChunkKey{key}).field_span<Open>();
    std::fill(open.begin(), open.end(), true);
    auto costs = world->chunk(tess::ChunkKey{key}).field_span<Cost>();
    std::fill(costs.begin(), costs.end(), 1);
  }
  using Either = tess::movement::MovementClass<
      tess::movement::Field<Open>, tess::movement::FieldCost<Cost>,
      tess::movement::DiagonalSteps<
          tess::movement::CornerRule::RequireOneClear>>;
  tess::NavigationGrid<Sparse, Diagonal> both{world, 1};
  tess::NavigationGrid<Sparse, Either> either{std::move(world), 1};
  EXPECT_EQ(find_edge(both, {3, 3, 0}, {4, 4, 0}).availability,
            tess::NavigationAvailability::Unavailable);
  EXPECT_EQ(find_edge(either, {3, 3, 0}, {4, 4, 0}).availability,
            tess::NavigationAvailability::Legal);
  EXPECT_EQ(find_edge(either, {3, 3, 0}, {4, 4, 0}, true).availability,
            tess::NavigationAvailability::Legal);
}

struct PassageVeto {
  template <typename WorldType>
  auto operator()(const WorldType&, tess::Coord3 from,
                  tess::Coord3 to) const noexcept -> bool {
    return !((from.x == 1 && to.x == 2) || (from.x == 2 && to.x == 1));
  }
};

TEST(TessNavigationGrid, StandableNeighborsDoNotProvePhysicalPassage) {
  using CorridorShape =
      tess::Shape<tess::Extent3{4, 1, 1}, tess::Extent3{2, 1, 1}>;
  using Corridor = tess::AlwaysResidentWorld<CorridorShape, Schema>;
  auto world = std::make_shared<Corridor>();
  world->fill_field<Open>(true);
  world->fill_field<Cost>(1);
  auto grid =
      std::make_shared<tess::NavigationGrid<Corridor, Movement, PassageVeto>>(
          std::move(world), 1);
  auto snapshot = std::make_shared<tess::NavigationSnapshot>(
      std::vector<std::shared_ptr<const tess::NavigationDomain>>{grid});
  const auto start = grid->coordinate_location({0, 0, 0});
  const auto goal = grid->coordinate_location({3, 0, 0});
  const auto result = tess::navigation_route(snapshot, start, goal);
  ASSERT_NE(result, nullptr);
  EXPECT_EQ(result->outcome, tess::NavigationOutcome::NoRoute);
  tess::NavigationQuery guidance{snapshot, goal};
  while (guidance.state() == tess::AsyncResultState::Pending)
    guidance.advance({1});
  ASSERT_NE(guidance.result(), nullptr);
  EXPECT_EQ(guidance.result()->guidance_at(start), nullptr);
}

TEST(TessNavigationGrid, InvalidUnusedGridInvalidatesComposition) {
  auto invalid =
      std::make_shared<tess::NavigationGrid<World, Movement>>(open_world(), 0);
  tess::NavigationSnapshot snapshot{
      std::vector<std::shared_ptr<const tess::NavigationDomain>>{invalid}};
  EXPECT_FALSE(snapshot.valid());
}

TEST(TessNavigationGrid, UnavailableOnlyGoalDoesNotPublishFoundGuidance) {
  using Sparse = tess::SparseResidentWorld<Shape, Schema>;
  auto world =
      std::make_shared<Sparse>(tess::ResidencyConfig{Sparse::page_byte_size});
  auto grid = std::make_shared<tess::NavigationGrid<Sparse, Movement>>(
      std::move(world), 1);
  auto snapshot = std::make_shared<tess::NavigationSnapshot>(
      std::vector<std::shared_ptr<const tess::NavigationDomain>>{grid});
  tess::NavigationQuery query(snapshot, grid->coordinate_location({0, 0, 0}));
  while (query.state() == tess::AsyncResultState::Pending) query.advance({1});
  ASSERT_NE(query.result(), nullptr);
  EXPECT_EQ(query.result()->outcome, tess::NavigationOutcome::Indeterminate);
  EXPECT_TRUE(query.result()->guidance.empty());
}

}  // namespace
