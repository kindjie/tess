#include <gtest/gtest.h>
#include <tess/tess.h>

#include <cstdint>
#include <limits>
#include <span>
#include <type_traits>

namespace {

namespace mv = tess::movement;

struct PassableTag {};
struct ConstructionTag {};
struct CostTag {};

using PassableField = tess::Field<PassableTag, std::uint8_t>;
using ConstructionField = tess::Field<ConstructionTag, std::uint8_t>;
using CostField = tess::Field<CostTag, std::int32_t>;
using Schema = tess::FieldSchema<PassableField, ConstructionField, CostField>;

using Shape = tess::Shape<tess::Extent3{32, 32, 1}, tess::Extent3{16, 16, 1}>;
using Page = tess::ChunkPage<Shape, Schema>;

// Walker: passable terrain that is NOT under construction; plain terrain cost.
using Walker = mv::MovementClass<
    mv::AllOf<mv::Field<PassableTag>, mv::Not<mv::Field<ConstructionTag>>>,
    mv::FieldCost<CostTag>>;

// Builder: may enter passable OR construction tiles; construction tiles cost a
// fixed build price, everything else uses terrain cost.
using Builder = mv::MovementClass<
    mv::AnyOf<mv::Field<PassableTag>, mv::Field<ConstructionTag>>,
    mv::SelectCost<ConstructionTag, mv::ConstantCost<50>,
                   mv::FieldCost<CostTag>>>;

using Identity = mv::WalkableField<PassableTag>;
using ExplicitDefault =
    mv::MovementClass<mv::Field<PassableTag>, mv::UnitCost, mv::DefaultSteps>;
using DiagonalBoth = mv::DiagonalSteps<mv::CornerRule::RequireBothClear>;
using DiagonalEither = mv::DiagonalSteps<mv::CornerRule::RequireOneClear>;

using ThreeDimensional =
    tess::Shape<tess::Extent3{32, 32, 32}, tess::Extent3{16, 16, 16}>;
using OneDimensional =
    tess::Shape<tess::Extent3{32, 1, 1}, tess::Extent3{16, 1, 1}>;
using Hexagonal =
    tess::Shape<tess::Extent3{32, 32, 1}, tess::Extent3{16, 16, 1},
                tess::lattice::HexAxial>;

struct LegacyCustomClass : mv::movement_class_tag {
  static constexpr bool passable(const Page&, tess::LocalTileId) noexcept {
    return true;
  }

  static constexpr std::uint32_t entry_cost(const Page&,
                                            tess::LocalTileId) noexcept {
    return 1;
  }
};

Page make_page() { return Page{tess::ChunkKey{0}, tess::ChunkCoord3{0, 0, 0}}; }

void set_tile(Page& page, std::uint32_t idx, std::uint8_t passable,
              std::uint8_t construction, std::int32_t cost) {
  const tess::LocalTileId id{idx};
  page.field<PassableTag>(id) = passable;
  page.field<ConstructionTag>(id) = construction;
  page.field<CostTag>(id) = cost;
}

// --- type-level properties (no page needed) ----------------------------------

TEST(TessMovementClass, ClassesSatisfyTheConcept) {
  static_assert(mv::MovementClassFor<Walker, Page>);
  static_assert(mv::MovementClassFor<Builder, Page>);
  static_assert(mv::MovementClassFor<Identity, Page>);
  static_assert(mv::MovementClassFor<LegacyCustomClass, Page>);
  static_assert(!mv::MovementClassFor<int, Page>);
  SUCCEED();
}

TEST(TessMovementClass, MovementClassOfNormalizesTagsAndClasses) {
  // A raw field tag normalizes to the byte-identical WalkableField.
  static_assert(std::is_same_v<mv::movement_class_of<PassableTag>, Identity>);
  // An existing class passes through untouched.
  static_assert(std::is_same_v<mv::movement_class_of<Walker>, Walker>);
  // Only the identity classes advertise the field_span fast path.
  static_assert(mv::HasPassableSpan<Identity>);
  static_assert(!mv::HasPassableSpan<Walker>);
  SUCCEED();
}

TEST(TessMovementClass, DefaultsToStableOrthogonalStepPolicy) {
  static_assert(std::is_same_v<typename Walker::step_policy, mv::DefaultSteps>);
  static_assert(
      std::is_same_v<typename ExplicitDefault::step_policy, mv::DefaultSteps>);
  static_assert(std::is_same_v<mv::step_policy_of<Walker>, mv::DefaultSteps>);
  static_assert(
      std::is_same_v<mv::step_policy_of<LegacyCustomClass>, mv::DefaultSteps>);
  static_assert(mv::step_policy_identity_of<Walker> ==
                mv::StepPolicyIdentity::Default);
  static_assert(mv::step_policy_identity<DiagonalBoth> ==
                mv::StepPolicyIdentity::DiagonalRequireBothClear);
  static_assert(mv::step_policy_identity<DiagonalEither> ==
                mv::StepPolicyIdentity::DiagonalRequireOneClear);

  SUCCEED();
}

TEST(TessMovementClass, ValidatesStepPoliciesAgainstShapeLattices) {
  static_assert(mv::StepPolicyFor<mv::DefaultSteps, Shape>);
  static_assert(mv::StepPolicyFor<mv::DefaultSteps, Hexagonal>);
  static_assert(mv::StepPolicyFor<DiagonalBoth, Shape>);
  static_assert(mv::StepPolicyFor<DiagonalEither, Shape>);
  static_assert(!mv::StepPolicyFor<DiagonalBoth, OneDimensional>);
  static_assert(!mv::StepPolicyFor<DiagonalBoth, ThreeDimensional>);
  static_assert(!mv::StepPolicyFor<DiagonalBoth, Hexagonal>);
  static_assert(!mv::StepPolicyFor<int, Shape>);
  static_assert(mv::ValidCornerRule<mv::CornerRule::RequireBothClear>);
  static_assert(mv::ValidCornerRule<mv::CornerRule::RequireOneClear>);
  static_assert(!mv::ValidCornerRule<static_cast<mv::CornerRule>(255)>);

  SUCCEED();
}

// normalize_cost must be byte-exact with path.h tile_entry_cost_index.
TEST(TessMovementClass, NormalizeCostMatchesTheWeightedContract) {
  static_assert(mv::normalize_cost(std::int32_t{0}) == 0);   // zero impassable
  static_assert(mv::normalize_cost(std::int32_t{-7}) == 0);  // negative too
  static_assert(mv::normalize_cost(std::int32_t{5}) == 5);
  static_assert(mv::normalize_cost(std::uint32_t{0}) == 0);
  static_assert(mv::normalize_cost(std::uint32_t{9}) == 9);
  // Saturates through a u64 compare, exactly as the A* leaf does.
  static_assert(mv::normalize_cost(std::int64_t{1} << 40) ==
                std::numeric_limits<std::uint32_t>::max());
  static_assert(mv::normalize_cost(std::numeric_limits<std::uint32_t>::max()) ==
                std::numeric_limits<std::uint32_t>::max());
  SUCCEED();
}

// --- passability truth tables ------------------------------------------------

TEST(TessMovementClass, WalkerAvoidsConstructionBuilderEntersIt) {
  Page page = make_page();
  set_tile(page, 0, /*passable=*/1, /*construction=*/0, /*cost=*/5);
  set_tile(page, 1, /*passable=*/1, /*construction=*/1, /*cost=*/5);
  set_tile(page, 2, /*passable=*/0, /*construction=*/1, /*cost=*/5);
  set_tile(page, 3, /*passable=*/0, /*construction=*/0, /*cost=*/5);

  const tess::LocalTileId open{0};
  const tess::LocalTileId building{1};
  const tess::LocalTileId site{2};
  const tess::LocalTileId wall{3};

  // Open terrain: both classes may enter.
  EXPECT_TRUE(Walker::passable(page, open));
  EXPECT_TRUE(Builder::passable(page, open));

  // Passable-but-under-construction: only the Builder.
  EXPECT_FALSE(Walker::passable(page, building));
  EXPECT_TRUE(Builder::passable(page, building));

  // Impassable construction site: Walker no, Builder yes (AnyOf construction).
  EXPECT_FALSE(Walker::passable(page, site));
  EXPECT_TRUE(Builder::passable(page, site));

  // Plain wall: neither.
  EXPECT_FALSE(Walker::passable(page, wall));
  EXPECT_FALSE(Builder::passable(page, wall));
}

TEST(TessMovementClass, EntryCostReflectsTheClassExpression) {
  Page page = make_page();
  set_tile(page, 0, 1, 0, 5);   // open, cost 5
  set_tile(page, 1, 1, 1, 5);   // construction, terrain cost 5
  set_tile(page, 2, 1, 0, 0);   // cost 0 -> impassable weight
  set_tile(page, 3, 1, 0, -4);  // negative -> impassable weight

  const tess::LocalTileId open{0};
  const tess::LocalTileId building{1};
  const tess::LocalTileId zero{2};
  const tess::LocalTileId negative{3};

  // Walker uses terrain cost directly.
  EXPECT_EQ(Walker::entry_cost(page, open), 5u);
  EXPECT_EQ(Walker::entry_cost(page, zero), 0u);
  EXPECT_EQ(Walker::entry_cost(page, negative), 0u);

  // Builder pays the fixed build price on construction tiles, terrain cost off.
  EXPECT_EQ(Builder::entry_cost(page, open), 5u);
  EXPECT_EQ(Builder::entry_cost(page, building), 50u);

  // The unweighted identity class is always unit cost.
  EXPECT_EQ(Identity::entry_cost(page, open), 1u);
}

// --- identity byte-behavior + span fast path ---------------------------------

TEST(TessMovementClass, IdentityMatchesTheLegacySingleFieldScan) {
  Page page = make_page();
  set_tile(page, 0, 1, 0, 1);
  set_tile(page, 1, 0, 0, 1);

  const tess::LocalTileId open{0};
  const tess::LocalTileId blocked{1};

  // WalkableField::passable == the exact legacy static_cast<bool>(field).
  EXPECT_EQ(Identity::passable(page, open),
            static_cast<bool>(page.field<PassableTag>(open)));
  EXPECT_EQ(Identity::passable(page, blocked),
            static_cast<bool>(page.field<PassableTag>(blocked)));

  // The span fast path is the same storage the legacy flood scans.
  auto span = Identity::passable_span(page);
  static_assert(std::is_same_v<decltype(span), std::span<std::uint8_t>>);
  EXPECT_EQ(span.size(), Page::local_tile_count);
  EXPECT_EQ(&span[0], &page.field<PassableTag>(tess::LocalTileId{0}));
}

}  // namespace
