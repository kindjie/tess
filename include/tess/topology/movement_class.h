#pragma once

#include <tess/storage/chunk_page.h>
#include <tess/topology/step_policy.h>

#include <concepts>
#include <cstdint>
#include <limits>
#include <type_traits>

// Movement vocabulary DSL (M6). A MovementClass is a compile-time TYPE that
// fuses a passability predicate and an entry-cost expression, both composed
// from typed-field leaves. Every leaf reads the constexpr
// ChunkPage::field<Tag>(LocalTileId) at the (page, tile) seam -- world-scope
// accessors are not constexpr, so the vocabulary deliberately operates on a
// resolved page. Because the whole predicate inlines to the same &&/||/! a
// hand-written cast would emit, threading a class through labeling / A* /
// commit keeps single-field codegen (no std::function, no virtual).
namespace tess::movement {

// Marker base so movement_class_of can distinguish a class from a raw field tag
// without needing a Page type to probe the MovementClassFor concept.
/// Marks types that implement the compile-time movement-class contract.
struct movement_class_tag {};

// --- boolean TERMS over typed fields -----------------------------------------

// Truthy iff the named field is truthy at the tile.
/// Evaluates the truthiness of field `Tag` at a resolved tile.
template <typename Tag>
struct Field {
  template <typename Page>
  [[nodiscard]] static constexpr bool eval(const Page& page,
                                           LocalTileId id) noexcept {
    static_assert(Page::schema_type::template contains<Tag>,
                  "MovementClass references a field absent from the schema.");
    return static_cast<bool>(page.template field<Tag>(id));
  }
};

// Truthy iff the named (integral) field is non-zero -- e.g. a positive weight.
/// Evaluates whether integral field `Tag` is nonzero at a resolved tile.
template <typename Tag>
struct NotZero {
  template <typename Page>
  [[nodiscard]] static constexpr bool eval(const Page& page,
                                           LocalTileId id) noexcept {
    static_assert(Page::schema_type::template contains<Tag>,
                  "MovementClass references a field absent from the schema.");
    return page.template field<Tag>(id) != 0;
  }
};

/// Negates one compile-time passability term.
template <typename Term>
struct Not {
  template <typename Page>
  [[nodiscard]] static constexpr bool eval(const Page& page,
                                           LocalTileId id) noexcept {
    return !Term::eval(page, id);
  }
};

/// Requires every supplied passability term to evaluate true.
template <typename... Terms>
struct AllOf {
  template <typename Page>
  [[nodiscard]] static constexpr bool eval(const Page& page,
                                           LocalTileId id) noexcept {
    return (true && ... && Terms::eval(page, id));
  }
};

/// Requires at least one supplied passability term to evaluate true.
template <typename... Terms>
struct AnyOf {
  template <typename Page>
  [[nodiscard]] static constexpr bool eval(const Page& page,
                                           LocalTileId id) noexcept {
    return (false || ... || Terms::eval(page, id));
  }
};

// --- cost normalization ------------------------------------------------------

// Byte-exact match to path::detail::tile_entry_cost_index (path.h): 0 (or any
// non-positive signed value) means impassable, and the result saturates to
// u32. The overflow compare casts through u64 first, exactly as the A* leaf
// does, so a class-driven cost read is bit-identical to the legacy read.
/// Converts an integral entry cost to the movement cost domain.
///
/// Nonpositive values become impassable cost zero and positive overflow
/// saturates at `uint32_t` maximum.
template <typename Value>
[[nodiscard]] constexpr std::uint32_t normalize_cost(Value value) noexcept {
  static_assert(std::is_integral_v<std::remove_cvref_t<Value>>,
                "MovementClass cost field must be integral.");
  if constexpr (std::is_signed_v<std::remove_cvref_t<Value>>) {
    if (value <= 0) {
      return 0;
    }
  } else if (value == 0) {
    return 0;
  }
  if (static_cast<std::uint64_t>(value) >
      std::numeric_limits<std::uint32_t>::max()) {
    return std::numeric_limits<std::uint32_t>::max();
  }
  return static_cast<std::uint32_t>(value);
}

// --- cost EXPRESSIONS (0 == impassable, u32-saturated) -----------------------

/// Produces unit entry cost for every tile.
struct UnitCost {
  template <typename Page>
  [[nodiscard]] static constexpr std::uint32_t eval(const Page&,
                                                    LocalTileId) noexcept {
    return 1;
  }
};

/// Produces compile-time constant entry cost `N` for every tile.
template <std::uint32_t N>
struct ConstantCost {
  template <typename Page>
  [[nodiscard]] static constexpr std::uint32_t eval(const Page&,
                                                    LocalTileId) noexcept {
    return N;
  }
};

/// Reads and normalizes integral entry cost field `CostTag`.
template <typename CostTag>
struct FieldCost {
  template <typename Page>
  [[nodiscard]] static constexpr std::uint32_t eval(const Page& page,
                                                    LocalTileId id) noexcept {
    static_assert(Page::schema_type::template contains<CostTag>,
                  "MovementClass references a field absent from the schema.");
    return normalize_cost(page.template field<CostTag>(id));
  }
};

// cost = SelTag(truthy) ? WhenSet::eval(page, id) : WhenClear::eval(page, id).
// WhenSet/WhenClear are cost EXPRESSION types, not values.
/// Selects between two cost expressions using field `SelTag`.
template <typename SelTag, typename WhenSet, typename WhenClear>
struct SelectCost {
  template <typename Page>
  [[nodiscard]] static constexpr std::uint32_t eval(const Page& page,
                                                    LocalTileId id) noexcept {
    static_assert(Page::schema_type::template contains<SelTag>,
                  "MovementClass references a field absent from the schema.");
    return static_cast<bool>(page.template field<SelTag>(id))
               ? WhenSet::eval(page, id)
               : WhenClear::eval(page, id);
  }
};

// --- the class ---------------------------------------------------------------

/// Combines compile-time passability and entry-cost expressions.
template <typename PassExpr, typename CostExpr,
          typename StepPolicyT = DefaultSteps>
struct MovementClass : movement_class_tag {
  using pass_expr = PassExpr;
  using cost_expr = CostExpr;
  using step_policy = StepPolicyT;

  template <typename Page>
  [[nodiscard]] static constexpr bool passable(const Page& page,
                                               LocalTileId id) noexcept {
    return PassExpr::eval(page, id);
  }

  template <typename Page>
  [[nodiscard]] static constexpr std::uint32_t entry_cost(
      const Page& page, LocalTileId id) noexcept {
    return CostExpr::eval(page, id);
  }
};

// A class C is usable against pages of type Page.
/// Checks that `C` provides movement operations compatible with `Page`.
template <typename C, typename Page>
concept MovementClassFor = std::derived_from<C, movement_class_tag> &&
                           requires(const Page& page, LocalTileId id) {
                             {
                               C::passable(page, id)
                             } -> std::convertible_to<bool>;
                             {
                               C::entry_cost(page, id)
                             } -> std::convertible_to<std::uint32_t>;
                           };

// --- identity / default classes (backward compatibility) ---------------------

// The identity class for the legacy single-field, unweighted world. It is a
// distinct struct (NOT an alias) so it can carry the raw passability tag and
// expose passable_span: per-class region labeling uses that fast path to keep
// the identity flood a byte-identical field_span<Tag> scan.
/// Adapts a truthy passability field to unit-cost movement.
template <typename PassableTag>
struct WalkableField : MovementClass<Field<PassableTag>, UnitCost> {
  using passable_tag = PassableTag;

  template <typename Page>
  [[nodiscard]] static constexpr auto passable_span(Page& page) noexcept {
    return page.template field_span<PassableTag>();
  }
};

// Weighted identity that folds cost>0 into passability, so the region graph and
// the weighted search agree exactly (recommended for new weighted classes).
// Deliberately does NOT advertise the span fast path: its passability reads
// two fields, so a raw single-field span scan would label cost-zero tiles.
/// Combines a truthy field and positive cost field into weighted movement.
template <typename PassableTag, typename CostTag>
struct WalkableCostField
    : MovementClass<AllOf<Field<PassableTag>, NotZero<CostTag>>,
                    FieldCost<CostTag>> {};

// Weighted class preserving the legacy asymmetry: passability ignores cost
// (graph is more permissive than the weighted search, never the reverse), used
// by the legacy <World, PassableTag, CostTag> forwarders.
/// Preserves legacy weighted semantics where cost does not affect passability.
template <typename PassableTag, typename CostTag>
using LegacyWeighted = MovementClass<Field<PassableTag>, FieldCost<CostTag>>;

// True for classes that expose the field_span fast path. `passable_tag` is
// the advertisement: a class declaring it promises its passability predicate
// is exactly the raw truthiness of that one field and MUST provide the
// matching passable_span (the topology flood scans it verbatim). Composed
// classes -- including WalkableCostField, whose predicate reads two fields --
// must not declare it.
/// Checks whether a movement class advertises the exact field-span fast path.
template <typename C>
concept HasPassableSpan = requires { typename C::passable_tag; };

// Normalize a template argument that is EITHER a movement class OR a raw field
// tag, so every legacy <World, PassableTag> call site compiles unchanged and
// resolves to the byte-identical WalkableField.
/// Normalizes either a movement class or a raw passability field tag.
template <typename T>
using movement_class_of =
    std::conditional_t<std::derived_from<T, movement_class_tag>, T,
                       WalkableField<T>>;

namespace detail {

// Adapts any normalized class to unit entry costs while retaining its
// passability predicate and regular-step policy. Used by minimum-step APIs.
template <typename ClassOrTag>
struct UnitMovementClass : movement_class_tag {
  using source_class = movement_class_of<ClassOrTag>;
  using step_policy = step_policy_of<source_class>;

  template <typename Page>
  [[nodiscard]] static constexpr bool passable(const Page& page,
                                               LocalTileId id) noexcept {
    return source_class::passable(page, id);
  }

  template <typename Page>
  [[nodiscard]] static constexpr std::uint32_t entry_cost(
      const Page&, LocalTileId) noexcept {
    return 1;
  }
};

}  // namespace detail

}  // namespace tess::movement
