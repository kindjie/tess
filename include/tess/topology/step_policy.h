#pragma once

#include <tess/core/lattice.h>
#include <tess/core/shape.h>

#include <concepts>
#include <cstdint>
#include <type_traits>

namespace tess::movement {

/** Clearance requirement for a two-axis diagonal step. */
enum class CornerRule : std::uint8_t {
  RequireBothClear,
  RequireOneClear,
};

/** Stable identifier persisted with step-policy-dependent products. */
enum class StepPolicyIdentity : std::uint32_t {
  Default = 0x44454654,
  DiagonalRequireBothClear = 0x44474243,
  DiagonalRequireOneClear = 0x44474f43,
};

/** Selects the lattice's default regular transitions. */
struct DefaultSteps {
  static constexpr StepPolicyIdentity identity = StepPolicyIdentity::Default;
  static constexpr std::uint32_t cost_scale = 1;
};

/** Enables face steps and clearance-preserving two-axis diagonals. */
template <CornerRule Rule = CornerRule::RequireBothClear>
struct DiagonalSteps {
  static constexpr CornerRule corner_rule = Rule;
  static constexpr StepPolicyIdentity identity =
      Rule == CornerRule::RequireBothClear
          ? StepPolicyIdentity::DiagonalRequireBothClear
          : StepPolicyIdentity::DiagonalRequireOneClear;
  static constexpr std::uint32_t cost_scale = 128;
};

/** Checks whether a value names one of the supported corner rules. */
template <CornerRule Rule>
concept ValidCornerRule =
    Rule == CornerRule::RequireBothClear || Rule == CornerRule::RequireOneClear;

namespace detail {

template <typename Policy>
struct is_diagonal_steps : std::false_type {};

template <CornerRule Rule>
struct is_diagonal_steps<DiagonalSteps<Rule>> : std::true_type {
  static constexpr CornerRule corner_rule = Rule;
};

template <typename Shape>
inline constexpr auto effective_axis_count =
    static_cast<unsigned>(ShapeTraits<Shape>::size.x != 1) +
    static_cast<unsigned>(ShapeTraits<Shape>::size.y != 1) +
    static_cast<unsigned>(ShapeTraits<Shape>::size.z != 1);

template <typename Class, typename = void>
struct step_policy_of_impl {
  using type = DefaultSteps;
};

template <typename Class>
struct step_policy_of_impl<Class, std::void_t<typename Class::step_policy>> {
  using type = typename Class::step_policy;
};

}  // namespace detail

/** Checks the basic static contract required by a step policy. */
template <typename Policy>
concept StepPolicy = requires {
  { Policy::identity } -> std::convertible_to<StepPolicyIdentity>;
  { Policy::cost_scale } -> std::convertible_to<std::uint32_t>;
} && Policy::cost_scale > 0;

/** Checks whether a regular step policy is valid for a shape's lattice. */
template <typename Policy, typename Shape>
concept StepPolicyFor =
    StepPolicy<Policy> &&
    (std::is_same_v<Policy, DefaultSteps> ||
     (detail::is_diagonal_steps<Policy>::value &&
      ValidCornerRule<detail::is_diagonal_steps<Policy>::corner_rule> &&
      std::is_same_v<typename ShapeTraits<Shape>::lattice_type,
                     lattice::Orthogonal> &&
      detail::effective_axis_count<Shape> == 2));

/** Resolves legacy custom movement classes to default regular steps. */
template <typename Class>
using step_policy_of = typename detail::step_policy_of_impl<Class>::type;

/** Returns a step policy's stable identifier. */
template <typename Policy>
inline constexpr StepPolicyIdentity step_policy_identity = Policy::identity;

/** Returns a movement class's normalized stable step-policy identifier. */
template <typename Class>
inline constexpr StepPolicyIdentity step_policy_identity_of =
    step_policy_identity<step_policy_of<Class>>;

}  // namespace tess::movement
