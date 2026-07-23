#include <gtest/gtest.h>
#include <tess/tess.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace {

namespace mv = tess::movement;

struct PassableTag {};
struct CostTag {};

using Schema = tess::FieldSchema<tess::Field<PassableTag, bool>,
                                 tess::Field<CostTag, std::uint32_t>>;
using Square = tess::Shape<tess::Extent3{8, 8, 1}, tess::Extent3{4, 4, 1}>;
using Hex = tess::Shape<tess::Extent3{8, 8, 1}, tess::Extent3{4, 4, 1},
                        tess::lattice::HexAxial>;
using SparseShape = tess::Shape<tess::Extent3{8, 4, 1}, tess::Extent3{4, 4, 1}>;
using SquareWorld = tess::AlwaysResidentWorld<Square, Schema>;
using HexWorld = tess::AlwaysResidentWorld<Hex, Schema>;
using SparseWorld = tess::SparseResidentWorld<SparseShape, Schema>;

using DefaultClass =
    mv::MovementClass<mv::Field<PassableTag>, mv::FieldCost<CostTag>>;
using DiagonalBoth =
    mv::MovementClass<mv::Field<PassableTag>, mv::FieldCost<CostTag>,
                      mv::DiagonalSteps<mv::CornerRule::RequireBothClear>>;
using DiagonalEither =
    mv::MovementClass<mv::Field<PassableTag>, mv::FieldCost<CostTag>,
                      mv::DiagonalSteps<mv::CornerRule::RequireOneClear>>;

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

}  // namespace
