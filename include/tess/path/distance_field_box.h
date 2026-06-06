#pragma once

#include <tess/path/path.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace tess {

template <typename World, typename PassableTag, typename CostTag>
auto build_weighted_distance_field_in_box(const World& world, Coord3 goal,
                                          Box3 domain,
                                          DistanceFieldScratch& scratch)
    -> DistanceFieldResult {
  using Shape = typename World::shape_type;
  constexpr auto infinite_distance = std::numeric_limits<std::uint32_t>::max();

  TESS_DIAG_EVENT_VALUE(path_clear, scratch.touched_.size());
  scratch.clear_build();
  if (!contains<Shape>(goal) || !contains(domain, goal)) {
    return DistanceFieldResult{PathStatus::InvalidGoal, 0, 0};
  }
  TESS_DIAG_EVENT(path_goal_passability_check);
  if (!detail::is_passable<World, PassableTag>(world, goal)) {
    return DistanceFieldResult{PathStatus::InvalidGoal, 0, 0};
  }

  const auto goal_index = detail::tile_index<Shape>(goal);
  if (detail::tile_entry_cost_index<World, CostTag>(world, goal_index) == 0) {
    return DistanceFieldResult{PathStatus::InvalidGoal, 0, 0};
  }

  const auto node_count = detail::tile_count<World>();
  if (scratch.distance_.size() != node_count) {
    TESS_DIAG_EVENT(path_initialize);
    scratch.generation_.assign(node_count, 0);
    scratch.distance_.assign(node_count, infinite_distance);
  }

  scratch.goal_ = goal;
  scratch.has_goal_ = true;
  scratch.distance_[static_cast<std::size_t>(goal_index)] = 0;
  scratch.touch_node(goal_index);
  TESS_DIAG_EVENT(path_touch_node);
  scratch.weighted_frontier_.push_back(PathScratch::OpenNode{goal_index, 0, 0});
  std::push_heap(scratch.weighted_frontier_.begin(),
                 scratch.weighted_frontier_.end(), detail::open_node_less);
  TESS_DIAG_EVENT(path_heap_push);

  std::size_t expanded_nodes = 0;
  while (!scratch.weighted_frontier_.empty()) {
    TESS_DIAG_EVENT(path_heap_pop);
    std::pop_heap(scratch.weighted_frontier_.begin(),
                  scratch.weighted_frontier_.end(), detail::open_node_less);
    const auto current = scratch.weighted_frontier_.back();
    scratch.weighted_frontier_.pop_back();

    const auto current_offset = static_cast<std::size_t>(current.index);
    const auto current_distance =
        scratch.distance_at(current_offset, infinite_distance);
    if (current.g != current_distance) {
      TESS_DIAG_EVENT_VALUE(path_skip_pop, false);
      continue;
    }
    ++expanded_nodes;

    const auto current_entry_cost =
        detail::tile_entry_cost_index<World, CostTag>(world, current.index);
    if (current_entry_cost == 0) {
      continue;
    }
    const auto current_coord = detail::tile_coord<Shape>(current.index);
    detail::for_each_indexed_axis_neighbor<Shape>(
        current_coord, current.index,
        [&](Coord3 neighbor_coord, std::uint64_t neighbor_index) {
          if (!contains(domain, neighbor_coord)) {
            return;
          }
          TESS_DIAG_EVENT(path_neighbor_candidate);
          const auto neighbor_offset = static_cast<std::size_t>(neighbor_index);
          TESS_DIAG_EVENT(path_relax_attempt);
          if (!scratch.is_current(neighbor_offset)) {
            TESS_DIAG_EVENT(path_passability_check);
            if (!detail::is_passable_index<World, PassableTag>(
                    world, neighbor_index)) {
              TESS_DIAG_EVENT(path_neighbor_blocked);
              return;
            }
            if (detail::tile_entry_cost_index<World, CostTag>(
                    world, neighbor_index) == 0) {
              TESS_DIAG_EVENT(path_neighbor_blocked);
              return;
            }
            scratch.distance_[neighbor_offset] = infinite_distance;
            scratch.touch_node(neighbor_index);
            TESS_DIAG_EVENT(path_touch_node);
          }

          const auto next_distance =
              detail::saturating_add(current_distance, current_entry_cost);
          if (next_distance == infinite_distance) {
            return;
          }
          if (next_distance <
              scratch.distance_at(neighbor_offset, infinite_distance)) {
            TESS_DIAG_EVENT(path_relax_success);
            scratch.distance_[neighbor_offset] = next_distance;
            scratch.weighted_frontier_.push_back(PathScratch::OpenNode{
                neighbor_index,
                next_distance,
                next_distance,
            });
            std::push_heap(scratch.weighted_frontier_.begin(),
                           scratch.weighted_frontier_.end(),
                           detail::open_node_less);
            TESS_DIAG_EVENT(path_heap_push);
          }
        });
  }

  return DistanceFieldResult{PathStatus::Found, expanded_nodes,
                             scratch.touched_.size()};
}

}  // namespace tess
