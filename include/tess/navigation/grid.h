#pragma once

#include <tess/navigation/domain.h>
#include <tess/storage/world.h>
#include <tess/topology/transition_model.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <type_traits>
#include <utility>

namespace tess {

/// Default owned grid policy: permits every native regular transition.
struct NavigationRegularEdges {
  template <typename World>
  [[nodiscard]] constexpr auto operator()(const World&, Coord3,
                                          Coord3) const noexcept -> bool {
    return true;
  }
};

/**
 * Retained native-grid domain with regular movement and an owned edge veto.
 *
 * World and every transitively referenced field/filter value must remain frozen
 * for the entire domain lifetime. A shared_ptr<const World> retains storage; it
 * does not prevent mutation through other aliases or synchronize readers. The
 * filter is copied/moved by value and receives ORIGINAL forward coordinates in
 * both traversal directions. It may reject swept passages between standable
 * cells; returning true makes no geometric safety claim. Filter exceptions
 * propagate and its execution/allocation time is outside the query work bound.
 * Movement-class passability and entry-cost functions must be noexcept, as
 * required by the native availability and diagonal-clearance probes.
 *
 * Node IDs are native packed tile keys, including sparse nonresident nodes.
 * Edge IDs are geometric forward ordinals, unique per original source node;
 * retain (domain identity, from node, id) to identify a directed local edge.
 * Each indexed lookup examines at most ten geometric regular candidates per
 * scan, independent of world size or residency. Native sparse page lookups
 * may scan hash collision clusters; their time is not bounded by that count. No
 * arbitrary push provider or special transition is admitted. Host special edges
 * use explicit connections. Sparse completeness is true only when every chunk
 * is currently resident.
 *
 * common_unit_scale multiplies the existing movement/diagonal cost ticks; it
 * must be positive. All multiplication is checked, UINT64_MAX is reserved, and
 * existing uint32 probe overflow remains Overflow rather than a legal saturated
 * cost. Scales define units explicitly; no relation between world geometry and
 * another domain's costs is inferred. Construction and probes allocate nothing
 * in adapter code; the retained world and caller filter have their own costs.
 */
template <typename World, typename MovementClass,
          typename EdgeFilter = NavigationRegularEdges>
class NavigationGrid final : public NavigationDomain {
 public:
  using shape_type = typename World::shape_type;
  using class_type = MovementClass;
  using model_type = ResolvedTransitionModel<World, MovementClass>;
  using step_policy = typename model_type::step_policy;

  static_assert(ShapeTraits<shape_type>::tile_key_bits <= 64,
                "Navigation grid nodes require native uint64 tile keys.");
  static_assert(
      movement::MovementClassFor<class_type, typename World::page_type>,
      "NavigationGrid requires an explicit movement class.");
  static_assert(
      noexcept(class_type::passable(
          std::declval<const typename World::page_type&>(), LocalTileId{})) &&
          noexcept(class_type::entry_cost(
              std::declval<const typename World::page_type&>(), LocalTileId{})),
      "NavigationGrid movement functions must be noexcept.");
  static_assert(requires(const EdgeFilter& filter, const World& world,
                         Coord3 from, Coord3 to) {
    { filter(world, from, to) } -> std::convertible_to<bool>;
  });

  NavigationGrid(std::shared_ptr<const World> world,
                 std::uint64_t common_unit_scale, EdgeFilter filter = {})
      : world_(std::move(world)),
        scale_(common_unit_scale),
        filter_(std::move(filter)) {}

  [[nodiscard]] auto valid() const noexcept -> bool override {
    return world_ && scale_ != 0;
  }

  /// Returns an invalid zero-domain location for an out-of-shape coordinate.
  [[nodiscard]] auto coordinate_location(Coord3 at) const noexcept
      -> NavigationLocation {
    return world_ && tess::contains<shape_type>(at)
               ? location(
                     static_cast<std::uint64_t>(tile_key<shape_type>(at).value))
               : NavigationLocation{};
  }

  [[nodiscard]] auto contains(std::uint64_t node) const noexcept
      -> bool override {
    using Traits = ShapeTraits<shape_type>;
    if (!world_) return false;
    if constexpr (Traits::tile_key_bits < 64) {
      if (node >= (std::uint64_t{1} << Traits::tile_key_bits)) return false;
    }
    const auto key = TileKey<shape_type>{node};
    return chunk_key<shape_type>(key).value < Traits::chunk_count &&
           local_tile_id<shape_type>(key).value < Traits::local_tile_count;
  }

  [[nodiscard]] auto availability(std::uint64_t node) const noexcept
      -> NavigationAvailability override {
    if (!contains(node) || scale_ == 0) return NavigationAvailability::Invalid;
    const auto resolved = world_->resolve(coordinate(node));
    const auto* page = world_->try_chunk(resolved.chunk_key);
    if (page == nullptr) return NavigationAvailability::Unavailable;
    return class_type::passable(*page, resolved.local_tile_id) &&
                   class_type::entry_cost(*page, resolved.local_tile_id) != 0
               ? NavigationAvailability::Legal
               : NavigationAvailability::Blocked;
  }

  [[nodiscard]] auto degree(std::uint64_t node, bool reverse) const noexcept
      -> std::size_t override {
    (void)reverse;
    if (!contains(node)) return 0;
    std::size_t count = 0;
    detail::for_each_regular_candidate<shape_type, step_policy>(
        coordinate(node), [&](auto) { ++count; });
    return count;
  }

  [[nodiscard]] auto edge(std::uint64_t node, std::size_t cursor,
                          bool reverse) const -> NavigationLocalEdge override {
    if (!contains(node))
      return {.availability = NavigationAvailability::Invalid};
    const auto at = coordinate(node);
    Coord3 other{};
    std::size_t ordinal = 0;
    bool found = false;
    detail::for_each_regular_candidate<shape_type, step_policy>(
        at, [&](auto candidate) {
          if (ordinal++ == cursor) {
            other = candidate.to;
            found = true;
          }
        });
    if (!found) return {.availability = NavigationAvailability::Invalid};
    const auto from = reverse ? other : at;
    const auto to = reverse ? at : other;
    NavigationLocalEdge result{
        .from = static_cast<std::uint64_t>(tile_key<shape_type>(from).value),
        .to = static_cast<std::uint64_t>(tile_key<shape_type>(to).value),
        .availability = NavigationAvailability::Blocked};
    ordinal = 0;
    std::uint32_t multiplier = 1;
    detail::for_each_regular_candidate<shape_type, step_policy>(
        from, [&](auto candidate) {
          if (candidate.to == to) {
            result.id = ordinal;
            multiplier = candidate.multiplier;
          }
          ++ordinal;
        });
    const auto source = availability(result.from);
    if (source != NavigationAvailability::Legal) {
      result.availability = source;
      return result;
    }
    if (!filter_(*world_, from, to)) return result;
    const auto destination = availability(result.to);
    if (destination != NavigationAvailability::Legal) {
      result.availability = destination;
      return result;
    }
    const auto clearance = model_type::regular_availability(*world_, from, to);
    if (clearance == TransitionAvailability::Blocked) return result;
    if (clearance == TransitionAvailability::MissingTopology) {
      result.availability = NavigationAvailability::Unavailable;
      return result;
    }
    const auto resolved = world_->resolve(to);
    const auto* page = world_->try_chunk(resolved.chunk_key);
    const auto entry = static_cast<std::uint64_t>(
        class_type::entry_cost(*page, resolved.local_tile_id));
    // The legacy probe reserves the compact infinity before common scaling.
    // Check entry first, even for custom classes with a wider return type.
    if (entry >= std::numeric_limits<std::uint32_t>::max()) {
      result.availability = NavigationAvailability::Overflow;
      return result;
    }
    const auto cost = entry * multiplier;  // Two compact factors fit uint64.
    constexpr auto infinity = std::numeric_limits<std::uint64_t>::max();
    if (cost >= std::numeric_limits<std::uint32_t>::max() ||
        cost > (infinity - 1) / scale_) {
      result.availability = NavigationAvailability::Overflow;
      return result;
    }
    result.cost = cost * scale_;
    result.availability = NavigationAvailability::Legal;
    return result;
  }

  [[nodiscard]] auto complete() const noexcept -> bool override {
    if (!world_ || scale_ == 0) return false;
    if constexpr (std::is_same_v<typename World::residency_type,
                                 AlwaysResident>) {
      return true;
    } else {
      return world_->resident_count() == World::chunk_count;
    }
  }

 private:
  [[nodiscard]] static auto coordinate(std::uint64_t node) noexcept -> Coord3 {
    return coord<shape_type>(TileKey<shape_type>{node});
  }
  std::shared_ptr<const World> world_;
  std::uint64_t scale_;
  EdgeFilter filter_;
};

}  // namespace tess
