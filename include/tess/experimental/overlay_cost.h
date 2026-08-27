#pragma once

#include <tess/topology/movement_class.h>
#include <tess/topology/transition_model.h>

#include <cstdint>
#include <limits>

namespace tess::experimental {

// cost = Base == 0 ? 0 : saturating(Base + Overlay).
// Base/Overlay are cost EXPRESSION types, not values.
/**
 * Prices a base cost with an additive overlay, preserving impassability.
 *
 * `eval()` is zero if and only if `Base::eval()` is zero: an overlay
 * prices a tile the base already admits, and can never make an
 * impassable one enterable. The operands are therefore not
 * interchangeable, which is why this is not spelled as a sum. It
 * matches the rule the transition model already applies where a
 * provider's cost meets a class's entry cost: pricing an edge does not
 * override destination entry legality.
 *
 * The overlay's zero means "no surcharge". That is the one place in
 * this vocabulary where zero is not the impassable sentinel.
 *
 * Absorption is a backstop, not a substitute for putting the base's
 * impassability in the passability predicate. `NotZero<BaseTag>` there
 * is what keeps the region graph exact, and the minimum-step APIs that
 * substitute `UnitCost` for a class's cost expression see only the
 * predicate.
 *
 * Experimental: the spelling and the tier may change. The semantics are
 * settled -- they follow the transition model's existing provider-cost
 * rule -- but a stable cost expression is public surface a 1.x release
 * freezes, and this arrived during a release candidate's observation
 * window. Promotion belongs to the release that follows it.
 */
template <typename Base, typename Overlay>
struct OverlayCost {
  template <typename Page>
  [[nodiscard]] static constexpr std::uint32_t eval(const Page& page,
                                                    LocalTileId id) noexcept {
    const auto base = Base::eval(page, id);
    if (base == 0) {
      return 0;
    }
    const auto sum = static_cast<std::uint64_t>(base) +
                     static_cast<std::uint64_t>(Overlay::eval(page, id));
    constexpr auto ceiling =
        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max());
    return static_cast<std::uint32_t>(sum > ceiling ? ceiling : sum);
  }
};

}  // namespace tess::experimental

namespace tess::detail {

// Absorption only lowers the value, so the saturating sum of the two
// operand maxima stays a sound upper bound.
template <typename Base, typename Overlay, typename Schema>
struct CostExpressionMaximum<experimental::OverlayCost<Base, Overlay>, Schema> {
  using base_max = CostExpressionMaximum<Base, Schema>;
  using overlay_max = CostExpressionMaximum<Overlay, Schema>;
  static constexpr bool known = base_max::known && overlay_max::known;
  static constexpr std::uint32_t value = [] {
    const auto sum = static_cast<std::uint64_t>(base_max::value) +
                     static_cast<std::uint64_t>(overlay_max::value);
    constexpr auto ceiling =
        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max());
    return static_cast<std::uint32_t>(sum > ceiling ? ceiling : sum);
  }();
};

}  // namespace tess::detail
