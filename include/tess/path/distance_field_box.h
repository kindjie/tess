#pragma once

#include <tess/path/path.h>

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace tess {

/// Builds a weighted distance field constrained to `domain`.
///
/// Returns `InvalidGoal` when the goal is outside the world, domain, or
/// movement class. The caller owns `scratch`; reuse it to avoid allocations.
template <typename World, typename Class, typename Provider>
[[nodiscard]] auto build_weighted_distance_field_in_box(
    const World& world, Coord3 goal, Box3 domain, DistanceFieldScratch& scratch,
    [[maybe_unused]] MissingChunkPolicy policy, const Provider& provider)
    -> DistanceFieldResult {
  static_assert(std::derived_from<Class, movement::movement_class_tag>,
                "build_weighted_distance_field_in_box<World, Class> requires "
                "a MovementClass; pass a movement class such as "
                "PositiveCostFieldMovement.");
  using Shape = typename World::shape_type;
  using Space = detail::NodeIndexSpace<World>;
  using Model = ResolvedTransitionModel<World, Class, Provider>;
  constexpr auto infinite_distance = std::numeric_limits<std::uint32_t>::max();

  TESS_DIAG_EVENT_VALUE(path_clear, scratch.touched_.size());
  scratch.clear_build();
  if (!contains<Shape>(goal) || !contains(domain, goal)) {
    return DistanceFieldResult{PathStatus::InvalidGoal, 0, 0};
  }
  if constexpr (!Space::is_dense) {
    // A non-resident goal cannot seed the flood; resolve before is_passable /
    // the entry-cost read index the goal chunk.
    const Space residency{world};
    if (!residency.is_resident_index(detail::tile_index<Shape>(goal))) {
      return DistanceFieldResult{
          policy == MissingChunkPolicy::ReportIndeterminate
              ? PathStatus::Indeterminate
              : PathStatus::InvalidGoal,
          0, 0};
    }
  }
  TESS_DIAG_EVENT(path_goal_passability_check);
  if (!detail::is_passable<World, Class>(world, goal)) {
    return DistanceFieldResult{PathStatus::InvalidGoal, 0, 0};
  }

  const auto goal_index = detail::tile_index<Shape>(goal);
  if (detail::tile_entry_cost_index<World, Class>(world, goal_index) == 0) {
    return DistanceFieldResult{PathStatus::InvalidGoal, 0, 0};
  }

  const Space space{world};
  const auto node_count = space.capacity_hint();
  if (scratch.distance_.size() != node_count) {
    TESS_DIAG_EVENT(path_initialize);
    scratch.generation_.assign(node_count, 0);
    scratch.distance_.assign(node_count, infinite_distance);
  }

  const auto goal_offset = space.offset(goal_index);
  const auto model = Model{provider};
  scratch.goal_ = goal;
  scratch.has_goal_ = true;
  scratch.template stamp_model<Model>(
      model, detail::transition_provider_instance_identity(provider));
  scratch.stamp_residency(world);
  scratch.distance_[goal_offset] = 0;
  scratch.touch_node(goal_offset, goal_index);
  TESS_DIAG_EVENT(path_touch_node);
  scratch.weighted_frontier_.push_back(
      detail::PackedOpenNode::make(goal_index, 0, 0));
  std::push_heap(scratch.weighted_frontier_.begin(),
                 scratch.weighted_frontier_.end(),
                 detail::packed_open_node_less);
  TESS_DIAG_EVENT(path_heap_push);

  std::size_t expanded_nodes = 0;
  [[maybe_unused]] bool crossed_missing = false;
  auto cost_overflow = false;
  while (!scratch.weighted_frontier_.empty()) {
    TESS_DIAG_EVENT(path_heap_pop);
    std::pop_heap(scratch.weighted_frontier_.begin(),
                  scratch.weighted_frontier_.end(),
                  detail::packed_open_node_less);
    const auto current = scratch.weighted_frontier_.back();
    scratch.weighted_frontier_.pop_back();

    const auto current_offset = space.offset(current.index);
    const auto current_distance =
        scratch.distance_at(current_offset, infinite_distance);
    if (current.g() != current_distance) {
      TESS_DIAG_EVENT_VALUE(path_skip_pop, false);
      continue;
    }
    ++expanded_nodes;

    const auto current_coord = detail::tile_coord<Shape>(current.index);
    model.for_each_reverse(
        world, current_coord, current.index, [&](auto probe) {
          if (!contains(domain, probe.to)) {
            return;
          }
          TESS_DIAG_EVENT(path_neighbor_candidate);
          if (probe.availability == TransitionAvailability::MissingTopology) {
            crossed_missing = true;
            return;
          }
          if (probe.cost_overflow) {
            cost_overflow = true;
            return;
          }
          const auto neighbor_index = probe.to_index;
          if constexpr (!Space::is_dense) {
            if (!space.is_resident_index(neighbor_index)) {
              crossed_missing = true;
              return;
            }
          }
          const auto neighbor_offset = space.offset(neighbor_index);
          TESS_DIAG_EVENT(path_relax_attempt);
          if (!scratch.is_current(neighbor_offset)) {
            scratch.distance_[neighbor_offset] = infinite_distance;
            scratch.touch_node(neighbor_offset, neighbor_index);
            TESS_DIAG_EVENT(path_touch_node);
          }

          const auto next_distance =
              detail::saturating_add(current_distance, probe.cost);
          if (next_distance == infinite_distance) {
            cost_overflow = true;
            return;
          }
          if (next_distance <
              scratch.distance_at(neighbor_offset, infinite_distance)) {
            TESS_DIAG_EVENT(path_relax_success);
            scratch.distance_[neighbor_offset] = next_distance;
            scratch.weighted_frontier_.push_back(detail::PackedOpenNode::make(
                neighbor_index, next_distance, next_distance));
            std::push_heap(scratch.weighted_frontier_.begin(),
                           scratch.weighted_frontier_.end(),
                           detail::packed_open_node_less);
            TESS_DIAG_EVENT(path_heap_push);
          }
        });
  }

  if constexpr (!Space::is_dense) {
    if (crossed_missing && policy == MissingChunkPolicy::ReportIndeterminate) {
      scratch.publish_build_status(PathStatus::Indeterminate);
      return DistanceFieldResult{PathStatus::Indeterminate, expanded_nodes,
                                 scratch.touched_.size()};
    }
  }
  if (cost_overflow) {
    scratch.discard_build_result();
    return DistanceFieldResult{PathStatus::CostOverflow, expanded_nodes,
                               scratch.touched_.size()};
  }
  scratch.publish_build_status(PathStatus::Found);
  return DistanceFieldResult{PathStatus::Found, expanded_nodes,
                             scratch.touched_.size()};
}

template <typename World, typename Class>
[[nodiscard]] auto build_weighted_distance_field_in_box(
    const World& world, Coord3 goal, Box3 domain, DistanceFieldScratch& scratch,
    MissingChunkPolicy policy) -> DistanceFieldResult {
  return build_weighted_distance_field_in_box<World, Class,
                                              AdjacentTransitions>(
      world, goal, domain, scratch, policy, AdjacentTransitions{});
}

}  // namespace tess
