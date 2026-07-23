#include <gtest/gtest.h>
#include <tess/tess.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace {

namespace mv = tess::movement;

struct PassableTag {};
struct CostTag {};
struct StairTag {};

using Schema = tess::FieldSchema<tess::Field<PassableTag, bool>,
                                 tess::Field<CostTag, std::uint32_t>>;
using Square = tess::Shape<tess::Extent3{8, 8, 1}, tess::Extent3{4, 4, 1}>;
using Hex = tess::Shape<tess::Extent3{8, 8, 1}, tess::Extent3{4, 4, 1},
                        tess::lattice::HexAxial>;
using SparseShape = tess::Shape<tess::Extent3{8, 4, 1}, tess::Extent3{4, 4, 1}>;
using SquareWorld = tess::AlwaysResidentWorld<Square, Schema>;
using HexWorld = tess::AlwaysResidentWorld<Hex, Schema>;
using SparseWorld = tess::SparseResidentWorld<SparseShape, Schema>;
using StairSchema = tess::FieldSchema<tess::Field<PassableTag, bool>,
                                      tess::Field<CostTag, std::uint32_t>,
                                      tess::Field<StairTag, std::uint8_t>>;
using StairShape = tess::Shape<tess::Extent3{4, 4, 2}, tess::Extent3{4, 4, 2}>;
using StairWorld = tess::AlwaysResidentWorld<StairShape, StairSchema>;

struct CostlyBridgeProvider {
  [[maybe_unused]] static constexpr std::uint32_t maximum_transition_cost = 3;

  template <typename WorldType, typename Sink>
  void for_each_forward(const WorldType&, tess::Coord3 from,
                        Sink&& sink) const {
    if (from == tess::Coord3{1, 1, 0}) {
      sink(tess::SpecialTransitionCandidate{.to = tess::Coord3{2, 1, 1},
                                            .cost = 3});
    }
  }

  template <typename WorldType, typename Sink>
  void for_each_reverse(const WorldType&, tess::Coord3 to, Sink&& sink) const {
    if (to == tess::Coord3{2, 1, 1}) {
      sink(tess::SpecialTransitionCandidate{.to = tess::Coord3{1, 1, 0},
                                            .cost = 3});
    }
  }
};

using DefaultClass =
    mv::MovementClass<mv::Field<PassableTag>, mv::FieldCost<CostTag>>;
using DiagonalBoth =
    mv::MovementClass<mv::Field<PassableTag>, mv::FieldCost<CostTag>,
                      mv::DiagonalSteps<mv::CornerRule::RequireBothClear>>;
using DiagonalEither =
    mv::MovementClass<mv::Field<PassableTag>, mv::FieldCost<CostTag>,
                      mv::DiagonalSteps<mv::CornerRule::RequireOneClear>>;

struct UnknownCostClass : mv::movement_class_tag {
  template <typename Page>
  static auto passable(const Page&, tess::LocalTileId) noexcept -> bool {
    return true;
  }

  template <typename Page>
  static auto entry_cost(const Page&, tess::LocalTileId) noexcept
      -> std::uint32_t {
    return 1;
  }
};

template <typename World>
void fill_open(World& world, std::uint32_t cost = 1) {
  for (auto& page : world.chunks()) {
    auto passable = page.template field_span<PassableTag>();
    std::fill(passable.begin(), passable.end(), true);
    auto costs = page.template field_span<CostTag>();
    std::fill(costs.begin(), costs.end(), cost);
  }
}

template <typename Shape>
auto tile_index(tess::Coord3 coord) -> std::uint64_t {
  return static_cast<std::uint64_t>(tess::tile_key<Shape>(coord).value);
}

template <std::size_t Capacity>
struct ProbeBuffer {
  std::array<tess::TransitionProbe<>, Capacity> probes{};
  std::size_t size = 0;

  void push(tess::TransitionProbe<> probe) {
    ASSERT_LT(size, Capacity);
    probes[size++] = probe;
  }
};

TEST(TessTransitionModel, ModelsSatisfyForwardAndReverseContracts) {
  using Orthogonal = tess::ResolvedTransitionModel<SquareWorld, DefaultClass>;
  using Diagonal = tess::ResolvedTransitionModel<SquareWorld, DiagonalBoth>;
  using Axial = tess::ResolvedTransitionModel<HexWorld, DefaultClass>;

  static_assert(tess::ForwardTransitionModelFor<Orthogonal, SquareWorld>);
  static_assert(tess::ReverseTransitionModelFor<Orthogonal, SquareWorld>);
  static_assert(tess::ForwardTransitionModelFor<Diagonal, SquareWorld>);
  static_assert(tess::ReverseTransitionModelFor<Diagonal, SquareWorld>);
  static_assert(tess::ForwardTransitionModelFor<Axial, HexWorld>);
  static_assert(tess::ReverseTransitionModelFor<Axial, HexWorld>);
  static_assert(Orthogonal::cost_scale == 1);
  static_assert(Diagonal::cost_scale == 128);
  static_assert(Axial::cost_scale == 1);
  SUCCEED();
}

TEST(TessTransitionModel, AssessesCompactCostRangeConservatively) {
  using Unit = mv::WalkableField<PassableTag>;
  using UnitDiagonal = mv::MovementClass<mv::Field<PassableTag>, mv::UnitCost,
                                         mv::DiagonalSteps<>>;

  static_assert(tess::path_cost_range_assessment<SquareWorld, Unit> ==
                tess::CostRangeAssessment::ProvenSafe);
  static_assert(tess::path_cost_range_assessment<SquareWorld, UnitDiagonal> ==
                tess::CostRangeAssessment::ProvenSafe);
  static_assert(tess::path_cost_range_assessment<SquareWorld, DefaultClass> ==
                tess::CostRangeAssessment::PotentialOverflow);
  static_assert(
      tess::path_cost_range_assessment<SquareWorld, UnknownCostClass> ==
      tess::CostRangeAssessment::Unknown);
  constexpr auto proof_compiles = [] {
    tess::require_proven_path_cost_range<SquareWorld, Unit>();
    return true;
  }();
  static_assert(proof_compiles);
  SUCCEED();
}

TEST(TessTransitionModel, EmitsCanonicalOrthogonalAndDiagonalOrder) {
  SquareWorld world;
  fill_open(world);
  constexpr auto from = tess::Coord3{3, 3, 0};

  ProbeBuffer<8> orthogonal;
  tess::ResolvedTransitionModel<SquareWorld, DefaultClass>{}.for_each_forward(
      world, from, tile_index<Square>(from),
      [&](auto probe) { orthogonal.push(probe); });
  ASSERT_EQ(orthogonal.size, 4u);
  EXPECT_EQ(orthogonal.probes[0].to, (tess::Coord3{4, 3, 0}));
  EXPECT_EQ(orthogonal.probes[1].to, (tess::Coord3{2, 3, 0}));
  EXPECT_EQ(orthogonal.probes[2].to, (tess::Coord3{3, 4, 0}));
  EXPECT_EQ(orthogonal.probes[3].to, (tess::Coord3{3, 2, 0}));
  EXPECT_EQ(orthogonal.probes[0].cost, 1u);

  ProbeBuffer<8> diagonal;
  tess::ResolvedTransitionModel<SquareWorld, DiagonalBoth>{}.for_each_forward(
      world, from, tile_index<Square>(from),
      [&](auto probe) { diagonal.push(probe); });
  ASSERT_EQ(diagonal.size, 8u);
  EXPECT_EQ(diagonal.probes[0].to, (tess::Coord3{4, 3, 0}));
  EXPECT_EQ(diagonal.probes[3].to, (tess::Coord3{3, 2, 0}));
  EXPECT_EQ(diagonal.probes[4].to, (tess::Coord3{4, 4, 0}));
  EXPECT_EQ(diagonal.probes[5].to, (tess::Coord3{4, 2, 0}));
  EXPECT_EQ(diagonal.probes[6].to, (tess::Coord3{2, 4, 0}));
  EXPECT_EQ(diagonal.probes[7].to, (tess::Coord3{2, 2, 0}));
  EXPECT_EQ(diagonal.probes[0].cost, 128u);
  EXPECT_EQ(diagonal.probes[4].cost, 181u);
}

TEST(TessTransitionModel, EnforcesBothDiagonalClearanceRules) {
  SquareWorld world;
  fill_open(world);
  world.field<PassableTag>(tess::Coord3{4, 3, 0}) = false;
  constexpr auto from = tess::Coord3{3, 3, 0};

  ProbeBuffer<8> both;
  tess::ResolvedTransitionModel<SquareWorld, DiagonalBoth>{}.for_each_forward(
      world, from, tile_index<Square>(from),
      [&](auto probe) { both.push(probe); });
  EXPECT_EQ(both.size, 5u);

  ProbeBuffer<8> either;
  tess::ResolvedTransitionModel<SquareWorld, DiagonalEither>{}.for_each_forward(
      world, from, tile_index<Square>(from),
      [&](auto probe) { either.push(probe); });
  EXPECT_EQ(either.size, 7u);
}

TEST(TessTransitionModel, EmitsCanonicalAxialOrder) {
  HexWorld world;
  fill_open(world);
  constexpr auto from = tess::Coord3{3, 3, 0};
  ProbeBuffer<6> probes;

  tess::ResolvedTransitionModel<HexWorld, DefaultClass>{}.for_each_forward(
      world, from, tile_index<Hex>(from),
      [&](auto probe) { probes.push(probe); });

  ASSERT_EQ(probes.size, 6u);
  EXPECT_EQ(probes.probes[0].to, (tess::Coord3{4, 3, 0}));
  EXPECT_EQ(probes.probes[1].to, (tess::Coord3{2, 3, 0}));
  EXPECT_EQ(probes.probes[2].to, (tess::Coord3{3, 4, 0}));
  EXPECT_EQ(probes.probes[3].to, (tess::Coord3{3, 2, 0}));
  EXPECT_EQ(probes.probes[4].to, (tess::Coord3{4, 2, 0}));
  EXPECT_EQ(probes.probes[5].to, (tess::Coord3{2, 4, 0}));
}

TEST(TessTransitionModel, ReverseTraversalChargesForwardDestination) {
  SquareWorld world;
  fill_open(world, 2);
  constexpr auto to = tess::Coord3{3, 3, 0};
  world.field<CostTag>(to) = 7;
  ProbeBuffer<4> probes;

  tess::ResolvedTransitionModel<SquareWorld, DefaultClass>{}.for_each_reverse(
      world, to, tile_index<Square>(to),
      [&](auto probe) { probes.push(probe); });

  ASSERT_EQ(probes.size, 4u);
  for (std::size_t i = 0; i < probes.size; ++i) {
    EXPECT_EQ(probes.probes[i].cost, 7u);
  }
}

TEST(TessTransitionModel, ReportsMissingSparseTargets) {
  SparseWorld world{tess::ResidencyConfig{SparseWorld::page_byte_size}};
  world.ensure_resident(tess::ChunkKey{0});
  auto& page = world.chunk(tess::ChunkKey{0});
  std::fill(page.field_span<PassableTag>().begin(),
            page.field_span<PassableTag>().end(), true);
  std::fill(page.field_span<CostTag>().begin(),
            page.field_span<CostTag>().end(), 1u);
  constexpr auto from = tess::Coord3{3, 2, 0};
  ProbeBuffer<4> probes;

  tess::ResolvedTransitionModel<SparseWorld, DefaultClass>{}.for_each_forward(
      world, from, tile_index<SparseShape>(from),
      [&](auto probe) { probes.push(probe); });

  ASSERT_EQ(probes.size, 4u);
  EXPECT_EQ(probes.probes[0].to, (tess::Coord3{4, 2, 0}));
  EXPECT_EQ(probes.probes[0].availability,
            tess::TransitionAvailability::MissingTopology);
}

TEST(TessTransitionModel, ComposesStairsAfterRegularEdgesInBothDirections) {
  StairWorld world;
  fill_open(world, 2);
  constexpr auto foot = tess::Coord3{1, 1, 0};
  constexpr auto landing = tess::Coord3{2, 1, 1};
  world.field<StairTag>(foot) =
      static_cast<std::uint8_t>(tess::StairDirection::PositiveX);
  world.field<CostTag>(landing) = 7;
  const auto provider = tess::StairTransitions<StairTag>{};
  using Model = tess::ResolvedTransitionModel<StairWorld, DefaultClass,
                                              tess::StairTransitions<StairTag>>;
  static_assert(
      tess::ForwardTransitionProviderFor<decltype(provider), StairWorld>);
  static_assert(
      tess::ReverseTransitionProviderFor<decltype(provider), StairWorld>);

  ProbeBuffer<8> forward;
  Model{provider}.for_each_forward(world, foot, tile_index<StairShape>(foot),
                                   [&](auto probe) { forward.push(probe); });
  ASSERT_EQ(forward.size, 6u);
  EXPECT_EQ(forward.probes[5].to, landing);
  EXPECT_EQ(forward.probes[5].kind, tess::TransitionKind::Special);
  EXPECT_EQ(forward.probes[5].availability,
            tess::TransitionAvailability::Legal);
  EXPECT_EQ(forward.probes[5].cost, 1u);

  ProbeBuffer<8> reverse;
  Model{provider}.for_each_reverse(world, landing,
                                   tile_index<StairShape>(landing),
                                   [&](auto probe) { reverse.push(probe); });
  ASSERT_EQ(reverse.size, 6u);
  EXPECT_EQ(reverse.probes[5].to, foot);
  EXPECT_EQ(reverse.probes[5].kind, tess::TransitionKind::Special);
  EXPECT_EQ(reverse.probes[5].cost, 1u);
  EXPECT_EQ(Model{provider}.revision(), 0u);
  EXPECT_EQ(Model{provider}.heuristic(world, foot, landing), 0u);
}

TEST(TessTransitionModel, ScalesProviderOwnedCostWithoutTerrainLookup) {
  StairWorld world;
  fill_open(world, 2);
  constexpr auto foot = tess::Coord3{1, 1, 0};
  constexpr auto landing = tess::Coord3{2, 1, 1};
  world.field<CostTag>(landing) = 99;
  using Model = tess::ResolvedTransitionModel<StairWorld, DefaultClass,
                                              CostlyBridgeProvider>;
  ProbeBuffer<8> forward;

  Model{CostlyBridgeProvider{}}.for_each_forward(
      world, foot, tile_index<StairShape>(foot),
      [&](auto probe) { forward.push(probe); });

  ASSERT_EQ(forward.size, 6u);
  EXPECT_EQ(forward.probes[5].to, landing);
  EXPECT_EQ(forward.probes[5].cost, 3u);
}

TEST(TessTransitionModel, EnumerationPropagatesSinkExceptions) {
  SquareWorld world;
  fill_open(world);
  constexpr auto from = tess::Coord3{3, 3, 0};

  EXPECT_THROW((tess::ResolvedTransitionModel<SquareWorld, DefaultClass>{}
                    .for_each_forward(
                        world, from, tile_index<Square>(from),
                        [](auto) { throw std::runtime_error{"sink failed"}; })),
               std::runtime_error);
}

}  // namespace
