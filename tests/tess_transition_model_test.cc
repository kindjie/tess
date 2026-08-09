#include <gtest/gtest.h>
#include <tess/tess.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

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
using SparseCornerShape =
    tess::Shape<tess::Extent3{8, 8, 1}, tess::Extent3{4, 4, 1}>;
using SquareWorld = tess::AlwaysResidentWorld<Square, Schema>;
using HexWorld = tess::AlwaysResidentWorld<Hex, Schema>;
using SparseWorld = tess::SparseResidentWorld<SparseShape, Schema>;
using SparseCornerWorld = tess::SparseResidentWorld<SparseCornerShape, Schema>;
using StairSchema = tess::FieldSchema<tess::Field<PassableTag, bool>,
                                      tess::Field<CostTag, std::uint32_t>,
                                      tess::Field<StairTag, std::uint8_t>>;
using StairShape = tess::Shape<tess::Extent3{4, 4, 2}, tess::Extent3{4, 4, 2}>;
using StairWorld = tess::AlwaysResidentWorld<StairShape, StairSchema>;

// Assessment needs only schema and count constants. This synthetic type makes
// (tile_count - 1) * 2 exactly 2^128, which wrapped to zero in the old
// multiply-then-compare implementation.
struct WideAssessmentWorld {
  using schema_type = Schema;
  static constexpr auto chunk_count =
      tess::UInt128::from_parts(std::uint64_t{1} << 63u, 1);
  static constexpr std::uint64_t local_tile_count = 1;
};

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

// The forward and reverse probes both reject an out-of-world coordinate.
// The dependency probe did not, and `chunk_coord` casts a negative
// component to unsigned, so the sink received an arbitrary out-of-range key
// -- which capture_field_product_dependencies uses to index an unchecked
// `seen` array.
TEST(TessTransitionModel, DependencyChunksRejectOutOfWorldOrigins) {
  SquareWorld world;
  const auto model = tess::ResolvedTransitionModel<SquareWorld, DefaultClass>{};

  const auto collect = [&](tess::Coord3 from) {
    std::vector<std::uint64_t> keys;
    model.for_each_dependency_chunk(
        world, from, [&](tess::ChunkKey key) { keys.push_back(key.value); });
    return keys;
  };

  // Sanity: an in-world origin still reports its own chunk at minimum.
  EXPECT_FALSE(collect(tess::Coord3{1, 1, 0}).empty());

  for (const auto outside :
       {tess::Coord3{-1, 0, 0}, tess::Coord3{0, -1, 0}, tess::Coord3{0, 0, -1},
        tess::Coord3{8, 0, 0}, tess::Coord3{0, 8, 0}}) {
    const auto keys = collect(outside);
    EXPECT_TRUE(keys.empty()) << "origin outside the world emitted a key";
    // Whatever is emitted must at least be indexable.
    for (const auto value : keys) {
      EXPECT_LT(value, SquareWorld::chunk_count);
    }
  }

  // The forward probe, whose behaviour this now matches, emits nothing.
  std::size_t forward = 0;
  model.for_each_forward(world, tess::Coord3{-1, 0, 0}, 0,
                         [&](auto) { ++forward; });
  EXPECT_EQ(forward, 0u);
}

TEST(TessTransitionModel, AssessesCompactCostRangeConservatively) {
  using Unit = mv::WalkableField<PassableTag>;
  using UnitDiagonal = mv::MovementClass<mv::Field<PassableTag>, mv::UnitCost,
                                         mv::DiagonalSteps<>>;
  using FixedTwo =
      mv::MovementClass<mv::Field<PassableTag>, mv::ConstantCost<2>>;

  static_assert(tess::path_cost_range_assessment<SquareWorld, Unit> ==
                tess::CostRangeAssessment::ProvenSafe);
  static_assert(tess::path_cost_range_assessment<SquareWorld, UnitDiagonal> ==
                tess::CostRangeAssessment::ProvenSafe);
  static_assert(tess::path_cost_range_assessment<SquareWorld, DefaultClass> ==
                tess::CostRangeAssessment::PotentialOverflow);
  static_assert(
      tess::path_cost_range_assessment<SquareWorld, UnknownCostClass> ==
      tess::CostRangeAssessment::Unknown);
  static_assert(
      tess::path_cost_range_assessment<WideAssessmentWorld, FixedTwo> ==
      tess::CostRangeAssessment::PotentialOverflow);
  constexpr auto proof_compiles = [] {
    tess::require_proven_path_cost_range<SquareWorld, Unit>();
    return true;
  }();
  static_assert(proof_compiles);
  constexpr auto widest =
      tess::UInt128::from_parts(std::numeric_limits<std::uint64_t>::max(),
                                std::numeric_limits<std::uint64_t>::max());
  static_assert(tess::detail::compact_cost_bound_overflows(widest, 2, 1));
  static_assert(
      !tess::detail::compact_cost_bound_overflows(tess::UInt128{1}, 1, 1));
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

TEST(TessTransitionModel, AxialCandidatesRejectCoordinatesOffZZeroPlane) {
  using Model = tess::ResolvedTransitionModel<HexWorld, DefaultClass>;

  EXPECT_FALSE(Model::is_regular_candidate(tess::Coord3{3, 3, 1},
                                           tess::Coord3{4, 3, 0}));
  EXPECT_FALSE(Model::is_regular_candidate(tess::Coord3{3, 3, -1},
                                           tess::Coord3{4, 3, 0}));
}

TEST(TessTransitionModel, ExtremeUncheckedOriginsEmitNoRegularCandidates) {
  using Orthogonal = tess::ResolvedTransitionModel<SquareWorld, DefaultClass>;
  using Diagonal = tess::ResolvedTransitionModel<SquareWorld, DiagonalBoth>;
  using Axial = tess::ResolvedTransitionModel<HexWorld, DefaultClass>;
  constexpr auto min = std::numeric_limits<std::int64_t>::min();
  constexpr auto max = std::numeric_limits<std::int64_t>::max();

  EXPECT_FALSE(Orthogonal::is_regular_candidate({max, 0, 0}, {0, 0, 0}));
  EXPECT_FALSE(Orthogonal::is_regular_candidate({min, 0, 0}, {0, 0, 0}));
  EXPECT_FALSE(Diagonal::is_regular_candidate({max, max, 0}, {0, 0, 0}));
  EXPECT_FALSE(Diagonal::is_regular_candidate({min, min, 0}, {0, 0, 0}));
  EXPECT_FALSE(Axial::is_regular_candidate({max, min, 0}, {0, 0, 0}));
}

TEST(TessTransitionModel, ReverseTraversalRejectsUncheckedExtremeTarget) {
  using Model = tess::ResolvedTransitionModel<SquareWorld, DefaultClass>;
  SquareWorld world;
  fill_open(world);
  constexpr auto max = std::numeric_limits<std::int64_t>::max();
  auto emitted = std::size_t{0};

  Model{}.for_each_reverse(world, {max, 0, 0}, 0, [&](auto) { ++emitted; });

  EXPECT_EQ(emitted, 0u);
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

TEST(TessTransitionModel, ReverseTraversalRejectsImpassableForwardTarget) {
  StairWorld world;
  fill_open(world, 1);
  constexpr auto foot = tess::Coord3{1, 1, 0};
  constexpr auto landing = tess::Coord3{2, 1, 1};
  world.field<StairTag>(foot) =
      static_cast<std::uint8_t>(tess::StairDirection::PositiveX);
  world.field<PassableTag>(landing) = false;
  using Model = tess::ResolvedTransitionModel<StairWorld, DefaultClass,
                                              tess::StairTransitions<StairTag>>;
  ProbeBuffer<8> probes;

  Model{tess::StairTransitions<StairTag>{}}.for_each_reverse(
      world, landing, tile_index<StairShape>(landing),
      [&](auto probe) { probes.push(probe); });

  EXPECT_EQ(probes.size, 0u);
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

TEST(TessTransitionModel,
     DiagonalClearanceUsesOnlyDecisionRelevantResidentTiles) {
  SparseCornerWorld world{
      tess::ResidencyConfig{3 * SparseCornerWorld::page_byte_size}};
  // From SW to NE crosses a chunk corner. Keep SW, NW, and the NE target
  // resident while the east clearance tile's SE chunk is absent.
  for (const auto key :
       {tess::ChunkKey{0}, tess::ChunkKey{2}, tess::ChunkKey{3}}) {
    world.ensure_resident(key);
    auto& page = world.chunk(key);
    std::fill(page.field_span<PassableTag>().begin(),
              page.field_span<PassableTag>().end(), true);
    std::fill(page.field_span<CostTag>().begin(),
              page.field_span<CostTag>().end(), 1u);
  }
  constexpr auto from = tess::Coord3{3, 3, 0};
  constexpr auto target = tess::Coord3{4, 4, 0};
  const auto target_status = [&](auto class_probe) {
    using Class = decltype(class_probe);
    auto status = tess::TransitionAvailability::Blocked;
    tess::ResolvedTransitionModel<SparseCornerWorld, Class>{}.for_each_forward(
        world, from, tile_index<SparseCornerShape>(from), [&](auto probe) {
          if (probe.to == target) {
            status = probe.availability;
          }
        });
    return status;
  };

  EXPECT_EQ(target_status(DiagonalBoth{}),
            tess::TransitionAvailability::MissingTopology);
  EXPECT_EQ(target_status(DiagonalEither{}),
            tess::TransitionAvailability::Legal);
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
