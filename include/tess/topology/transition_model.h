#pragma once

#include <tess/core/shape.h>
#include <tess/topology/movement_class.h>

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>

namespace tess {

/** Distinguishes regular lattice edges from provider-supplied edges. */
enum class TransitionKind : std::uint8_t {
  Regular,
  Special,
};

/** Three-valued result of probing a candidate transition. */
enum class TransitionAvailability : std::uint8_t {
  Legal,
  Blocked,
  MissingTopology,
};

/** Resolved transition data consumed by movement algorithms. */
template <typename Cost = std::uint32_t>
struct TransitionProbe {
  Coord3 to{};
  std::uint64_t to_index = 0;
  Cost cost = 0;
  TransitionKind kind = TransitionKind::Regular;
  TransitionAvailability availability = TransitionAvailability::Blocked;
  bool cost_overflow = false;
};

namespace detail {

struct RegularTransitionCandidate {
  Coord3 to{};
  Coord3 clearance_a{};
  Coord3 clearance_b{};
  std::uint32_t multiplier = 1;
  std::uint8_t clearance_count = 0;
};

template <typename Shape, typename Sink>
constexpr void emit_regular_candidate(Coord3 to, std::uint32_t multiplier,
                                      Sink&& sink) {
  if (contains<Shape>(to)) {
    sink(RegularTransitionCandidate{.to = to, .multiplier = multiplier});
  }
}

template <typename Shape, typename Sink>
constexpr void emit_diagonal_candidate(Coord3 to, Coord3 clearance_a,
                                       Coord3 clearance_b, Sink&& sink) {
  if (contains<Shape>(to)) {
    sink(RegularTransitionCandidate{
        .to = to,
        .clearance_a = clearance_a,
        .clearance_b = clearance_b,
        .multiplier = 181,
        .clearance_count = 2,
    });
  }
}

template <typename Shape, typename Sink>
constexpr void for_each_orthogonal_face_candidate(Coord3 from,
                                                  std::uint32_t multiplier,
                                                  Sink&& sink) {
  emit_regular_candidate<Shape>(Coord3{from.x + 1, from.y, from.z}, multiplier,
                                sink);
  emit_regular_candidate<Shape>(Coord3{from.x - 1, from.y, from.z}, multiplier,
                                sink);
  emit_regular_candidate<Shape>(Coord3{from.x, from.y + 1, from.z}, multiplier,
                                sink);
  emit_regular_candidate<Shape>(Coord3{from.x, from.y - 1, from.z}, multiplier,
                                sink);
  emit_regular_candidate<Shape>(Coord3{from.x, from.y, from.z + 1}, multiplier,
                                sink);
  emit_regular_candidate<Shape>(Coord3{from.x, from.y, from.z - 1}, multiplier,
                                sink);
}

template <typename Shape, typename Sink>
constexpr void for_each_hex_candidate(Coord3 from, Sink&& sink) {
  emit_regular_candidate<Shape>(Coord3{from.x + 1, from.y, 0}, 1, sink);
  emit_regular_candidate<Shape>(Coord3{from.x - 1, from.y, 0}, 1, sink);
  emit_regular_candidate<Shape>(Coord3{from.x, from.y + 1, 0}, 1, sink);
  emit_regular_candidate<Shape>(Coord3{from.x, from.y - 1, 0}, 1, sink);
  emit_regular_candidate<Shape>(Coord3{from.x + 1, from.y - 1, 0}, 1, sink);
  emit_regular_candidate<Shape>(Coord3{from.x - 1, from.y + 1, 0}, 1, sink);
}

template <typename Shape, typename Sink>
constexpr void for_each_diagonal_candidate(Coord3 from, Sink&& sink) {
  for_each_orthogonal_face_candidate<Shape>(from, 128, sink);
  if constexpr (!ShapeTraits<Shape>::degenerate_x &&
                !ShapeTraits<Shape>::degenerate_y) {
    emit_diagonal_candidate<Shape>(Coord3{from.x + 1, from.y + 1, from.z},
                                   Coord3{from.x + 1, from.y, from.z},
                                   Coord3{from.x, from.y + 1, from.z}, sink);
    emit_diagonal_candidate<Shape>(Coord3{from.x + 1, from.y - 1, from.z},
                                   Coord3{from.x + 1, from.y, from.z},
                                   Coord3{from.x, from.y - 1, from.z}, sink);
    emit_diagonal_candidate<Shape>(Coord3{from.x - 1, from.y + 1, from.z},
                                   Coord3{from.x - 1, from.y, from.z},
                                   Coord3{from.x, from.y + 1, from.z}, sink);
    emit_diagonal_candidate<Shape>(Coord3{from.x - 1, from.y - 1, from.z},
                                   Coord3{from.x - 1, from.y, from.z},
                                   Coord3{from.x, from.y - 1, from.z}, sink);
  } else if constexpr (!ShapeTraits<Shape>::degenerate_x &&
                       !ShapeTraits<Shape>::degenerate_z) {
    emit_diagonal_candidate<Shape>(Coord3{from.x + 1, from.y, from.z + 1},
                                   Coord3{from.x + 1, from.y, from.z},
                                   Coord3{from.x, from.y, from.z + 1}, sink);
    emit_diagonal_candidate<Shape>(Coord3{from.x + 1, from.y, from.z - 1},
                                   Coord3{from.x + 1, from.y, from.z},
                                   Coord3{from.x, from.y, from.z - 1}, sink);
    emit_diagonal_candidate<Shape>(Coord3{from.x - 1, from.y, from.z + 1},
                                   Coord3{from.x - 1, from.y, from.z},
                                   Coord3{from.x, from.y, from.z + 1}, sink);
    emit_diagonal_candidate<Shape>(Coord3{from.x - 1, from.y, from.z - 1},
                                   Coord3{from.x - 1, from.y, from.z},
                                   Coord3{from.x, from.y, from.z - 1}, sink);
  } else {
    emit_diagonal_candidate<Shape>(Coord3{from.x, from.y + 1, from.z + 1},
                                   Coord3{from.x, from.y + 1, from.z},
                                   Coord3{from.x, from.y, from.z + 1}, sink);
    emit_diagonal_candidate<Shape>(Coord3{from.x, from.y + 1, from.z - 1},
                                   Coord3{from.x, from.y + 1, from.z},
                                   Coord3{from.x, from.y, from.z - 1}, sink);
    emit_diagonal_candidate<Shape>(Coord3{from.x, from.y - 1, from.z + 1},
                                   Coord3{from.x, from.y - 1, from.z},
                                   Coord3{from.x, from.y, from.z + 1}, sink);
    emit_diagonal_candidate<Shape>(Coord3{from.x, from.y - 1, from.z - 1},
                                   Coord3{from.x, from.y - 1, from.z},
                                   Coord3{from.x, from.y, from.z - 1}, sink);
  }
}

template <typename Shape, typename Policy, typename Sink>
constexpr void for_each_regular_candidate(Coord3 from, Sink&& sink) {
  static_assert(movement::StepPolicyFor<Policy, Shape>);
  if constexpr (std::is_same_v<Policy, movement::DefaultSteps>) {
    if constexpr (std::is_same_v<typename ShapeTraits<Shape>::lattice_type,
                                 lattice::Orthogonal>) {
      for_each_orthogonal_face_candidate<Shape>(from, 1, sink);
    } else {
      for_each_hex_candidate<Shape>(from, sink);
    }
  } else {
    for_each_diagonal_candidate<Shape>(from, sink);
  }
}

template <typename Shape>
[[nodiscard]] constexpr auto transition_index(Coord3 coord) noexcept
    -> std::uint64_t {
  static_assert(ShapeTraits<Shape>::tile_key_bits <= 64,
                "Resolved transitions require u64 tile keys.");
  return static_cast<std::uint64_t>(tile_key<Shape>(coord).value);
}

[[nodiscard]] constexpr auto saturating_u32(UInt128 value) noexcept
    -> std::uint32_t {
  constexpr auto max = std::numeric_limits<std::uint32_t>::max();
  return value.hi != 0 || value.lo > max ? max
                                         : static_cast<std::uint32_t>(value.lo);
}

struct TransitionProbeSink {
  constexpr void operator()(TransitionProbe<>) const noexcept {}
};

struct ChunkKeySink {
  constexpr void operator()(ChunkKey) const noexcept {}
};

}  // namespace detail

/** Compile-time resolved lattice, movement-class, and step-policy model. */
template <typename World, typename ClassOrTag>
class ResolvedTransitionModel {
 public:
  using world_type = World;
  using shape_type = typename World::shape_type;
  using class_type = movement::movement_class_of<ClassOrTag>;
  using step_policy = movement::step_policy_of<class_type>;
  using cost_type = std::uint32_t;

  static_assert(movement::StepPolicyFor<step_policy, shape_type>,
                "Movement step policy is invalid for the world shape.");

  static constexpr auto lattice_identity =
      ShapeTraits<shape_type>::lattice_identity;
  static constexpr auto lattice_version =
      ShapeTraits<shape_type>::lattice_version;
  static constexpr auto step_policy_identity = step_policy::identity;
  static constexpr std::uint32_t cost_scale = step_policy::cost_scale;
  static constexpr bool preserves_default_connectivity =
      std::is_same_v<typename ShapeTraits<shape_type>::lattice_type,
                     lattice::Orthogonal>;

  template <typename Sink>
  constexpr void for_each_forward(const World& world, Coord3 from,
                                  std::uint64_t from_index,
                                  Sink&& sink) const noexcept {
    (void)from_index;
    detail::for_each_regular_candidate<shape_type, step_policy>(
        from, [&](detail::RegularTransitionCandidate candidate) {
          emit_candidate(world, candidate, candidate.to, sink);
        });
  }

  template <typename Sink>
  constexpr void for_each_reverse(const World& world, Coord3 to,
                                  std::uint64_t to_index,
                                  Sink&& sink) const noexcept {
    (void)to_index;
    detail::for_each_regular_candidate<shape_type, step_policy>(
        to, [&](detail::RegularTransitionCandidate candidate) {
          emit_candidate(world, candidate, to, sink);
        });
  }

  template <typename Sink>
  constexpr void for_each_dependency_chunk(const World&, Coord3 from,
                                           Sink&& sink) const noexcept {
    sink(chunk_key<shape_type>(chunk_coord<shape_type>(from)));
    detail::for_each_regular_candidate<shape_type, step_policy>(
        from, [&](detail::RegularTransitionCandidate candidate) {
          sink(chunk_key<shape_type>(chunk_coord<shape_type>(candidate.to)));
          if (candidate.clearance_count != 0) {
            sink(chunk_key<shape_type>(
                chunk_coord<shape_type>(candidate.clearance_a)));
            sink(chunk_key<shape_type>(
                chunk_coord<shape_type>(candidate.clearance_b)));
          }
        });
  }

  /** Returns whether `to` is a geometric regular-step candidate. */
  [[nodiscard]] static constexpr auto is_regular_candidate(Coord3 from,
                                                           Coord3 to) noexcept
      -> bool {
    auto found = false;
    detail::for_each_regular_candidate<shape_type, step_policy>(
        from, [&](detail::RegularTransitionCandidate candidate) {
          found = found || candidate.to == to;
        });
    return found;
  }

  /** Classifies static clearance for a geometric regular candidate. */
  [[nodiscard]] static constexpr auto regular_availability(const World& world,
                                                           Coord3 from,
                                                           Coord3 to) noexcept
      -> TransitionAvailability {
    auto availability = TransitionAvailability::Blocked;
    detail::for_each_regular_candidate<shape_type, step_policy>(
        from, [&](detail::RegularTransitionCandidate candidate) {
          if (candidate.to == to) {
            availability = clearance_availability(world, candidate);
          }
        });
    return availability;
  }

  [[nodiscard]] static constexpr auto heuristic(const World&, Coord3 from,
                                                Coord3 goal) noexcept
      -> cost_type {
    if constexpr (std::is_same_v<step_policy, movement::DefaultSteps>) {
      if constexpr (std::is_same_v<
                        typename ShapeTraits<shape_type>::lattice_type,
                        lattice::HexAxial>) {
        const auto distance =
            hex_distance(to_hex_coord(from), to_hex_coord(goal));
        return distance > std::numeric_limits<cost_type>::max()
                   ? std::numeric_limits<cost_type>::max()
                   : static_cast<cost_type>(distance);
      } else {
        const auto distance = manhattan_distance(from, goal);
        return distance > std::numeric_limits<cost_type>::max()
                   ? std::numeric_limits<cost_type>::max()
                   : static_cast<cost_type>(distance);
      }
    } else {
      return diagonal_heuristic(from, goal);
    }
  }

  [[nodiscard]] static constexpr auto revision() noexcept -> std::uint64_t {
    return 0;
  }

 private:
  enum class Clearance : std::uint8_t {
    Clear,
    Blocked,
    Missing,
  };

  [[nodiscard]] static constexpr auto clearance_at(const World& world,
                                                   Coord3 coord) noexcept
      -> Clearance {
    const auto resolved = world.resolve(coord);
    const auto* page = world.try_chunk(resolved.chunk_key);
    if (page == nullptr) {
      return Clearance::Missing;
    }
    return class_type::passable(*page, resolved.local_tile_id)
               ? Clearance::Clear
               : Clearance::Blocked;
  }

  [[nodiscard]] static constexpr auto clearance_availability(
      const World& world, detail::RegularTransitionCandidate candidate) noexcept
      -> TransitionAvailability {
    if constexpr (std::is_same_v<step_policy, movement::DefaultSteps>) {
      return TransitionAvailability::Legal;
    } else {
      if (candidate.clearance_count == 0) {
        return TransitionAvailability::Legal;
      }
      const auto a = clearance_at(world, candidate.clearance_a);
      const auto b = clearance_at(world, candidate.clearance_b);
      if constexpr (step_policy::corner_rule ==
                    movement::CornerRule::RequireBothClear) {
        if (a == Clearance::Blocked || b == Clearance::Blocked) {
          return TransitionAvailability::Blocked;
        }
        return a == Clearance::Missing || b == Clearance::Missing
                   ? TransitionAvailability::MissingTopology
                   : TransitionAvailability::Legal;
      } else {
        if (a == Clearance::Clear || b == Clearance::Clear) {
          return TransitionAvailability::Legal;
        }
        return a == Clearance::Missing || b == Clearance::Missing
                   ? TransitionAvailability::MissingTopology
                   : TransitionAvailability::Blocked;
      }
    }
  }

  template <typename Sink>
  static constexpr void emit_candidate(
      const World& world, detail::RegularTransitionCandidate candidate,
      Coord3 cost_coord, Sink&& sink) noexcept {
    const auto resolved = world.resolve(candidate.to);
    const auto* page = world.try_chunk(resolved.chunk_key);
    if (page == nullptr) {
      sink(TransitionProbe<>{
          .to = candidate.to,
          .to_index = detail::transition_index<shape_type>(candidate.to),
          .availability = TransitionAvailability::MissingTopology,
      });
      return;
    }
    if (!class_type::passable(*page, resolved.local_tile_id)) {
      return;
    }
    const auto clearance = clearance_availability(world, candidate);
    if (clearance == TransitionAvailability::Blocked) {
      return;
    }
    const auto cost_resolved = world.resolve(cost_coord);
    const auto* cost_page = world.try_chunk(cost_resolved.chunk_key);
    if (cost_page == nullptr) {
      sink(TransitionProbe<>{
          .to = candidate.to,
          .to_index = detail::transition_index<shape_type>(candidate.to),
          .availability = TransitionAvailability::MissingTopology,
      });
      return;
    }
    const auto entry_cost =
        class_type::entry_cost(*cost_page, cost_resolved.local_tile_id);
    if (entry_cost == 0) {
      return;
    }
    const auto precise_cost =
        detail::UInt128{entry_cost} * detail::UInt128{candidate.multiplier};
    constexpr auto infinite = std::numeric_limits<std::uint32_t>::max();
    sink(TransitionProbe<>{
        .to = candidate.to,
        .to_index = detail::transition_index<shape_type>(candidate.to),
        .cost = detail::saturating_u32(precise_cost),
        .availability = clearance,
        .cost_overflow = precise_cost.hi != 0 || precise_cost.lo >= infinite,
    });
  }

  [[nodiscard]] static constexpr auto diagonal_heuristic(Coord3 from,
                                                         Coord3 goal) noexcept
      -> cost_type {
    auto a = std::uint64_t{0};
    auto b = std::uint64_t{0};
    if constexpr (!ShapeTraits<shape_type>::degenerate_x &&
                  !ShapeTraits<shape_type>::degenerate_y) {
      a = detail::abs_delta(from.x, goal.x);
      b = detail::abs_delta(from.y, goal.y);
    } else if constexpr (!ShapeTraits<shape_type>::degenerate_x &&
                         !ShapeTraits<shape_type>::degenerate_z) {
      a = detail::abs_delta(from.x, goal.x);
      b = detail::abs_delta(from.z, goal.z);
    } else {
      a = detail::abs_delta(from.y, goal.y);
      b = detail::abs_delta(from.z, goal.z);
    }
    const auto minor = std::min(a, b);
    const auto major = std::max(a, b);
    const auto ticks =
        detail::add(detail::UInt128{minor} * detail::UInt128{181},
                    detail::UInt128{major - minor} * detail::UInt128{128});
    return detail::saturating_u32(ticks);
  }
};

/** Checks the hot forward-enumeration transition-model contract. */
template <typename Model, typename World>
concept ForwardTransitionModelFor = requires(
    const Model& model, const World& world, Coord3 from, std::uint64_t index) {
  model.for_each_forward(world, from, index, detail::TransitionProbeSink{});
  model.for_each_dependency_chunk(world, from, detail::ChunkKeySink{});
  { model.heuristic(world, from, from) } -> std::convertible_to<std::uint32_t>;
  { model.revision() } -> std::convertible_to<std::uint64_t>;
};

/** Checks forward plus reverse transition enumeration. */
template <typename Model, typename World>
concept ReverseTransitionModelFor =
    ForwardTransitionModelFor<Model, World> &&
    requires(const Model& model, const World& world, Coord3 to,
             std::uint64_t index) {
      model.for_each_reverse(world, to, index, detail::TransitionProbeSink{});
    };

}  // namespace tess
