#pragma once

#ifndef TESS_PATH_PATH_H_INCLUDED
#error "Include <tess/path/path.h> instead of this internal detail header."
#endif

template <typename World, typename PassableTag, typename CostTag,
          std::uint32_t MaxCost>
auto build_bounded_weighted_distance_field(const World& world, Coord3 goal,
                                           DistanceFieldScratch& scratch)
    -> DistanceFieldResult {
  static_assert(MaxCost > 0);
  using Shape = typename World::shape_type;
  constexpr auto infinite_distance = std::numeric_limits<std::uint32_t>::max();
  constexpr auto bucket_count = static_cast<std::size_t>(MaxCost) + 1u;

  TESS_DIAG_EVENT_VALUE(path_clear, scratch.touched_.size());
  scratch.clear_build();
  if (!contains<Shape>(goal)) {
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
  if (scratch.weighted_buckets_.size() != bucket_count) {
    scratch.weighted_buckets_.assign(bucket_count, {});
    for (auto& bucket : scratch.weighted_buckets_) {
      bucket.reserve(scratch.weighted_bucket_capacity_);
    }
  }

  scratch.goal_ = goal;
  scratch.has_goal_ = true;
  scratch.distance_[static_cast<std::size_t>(goal_index)] = 0;
  scratch.touch_node(goal_index);
  TESS_DIAG_EVENT(path_touch_node);
  scratch.weighted_buckets_[0].push_back(goal_index);
  TESS_DIAG_EVENT(path_heap_push);

  auto active_nodes = std::size_t{1};
  auto current_distance = std::uint32_t{0};
  std::size_t expanded_nodes = 0;
  while (active_nodes > 0) {
    auto& bucket = scratch.weighted_buckets_[current_distance % bucket_count];
    if (bucket.empty()) {
      ++current_distance;
      continue;
    }

    const auto current = bucket.back();
    bucket.pop_back();
    --active_nodes;
    TESS_DIAG_EVENT(path_heap_pop);

    const auto current_offset = static_cast<std::size_t>(current);
    const auto stored_distance =
        scratch.distance_at(current_offset, infinite_distance);
    if (stored_distance != current_distance) {
      if (stored_distance != infinite_distance &&
          stored_distance > current_distance) {
        scratch.weighted_buckets_[stored_distance % bucket_count].push_back(
            current);
        ++active_nodes;
      } else {
        TESS_DIAG_EVENT_VALUE(path_skip_pop, false);
      }
      continue;
    }
    ++expanded_nodes;

    const auto current_entry_cost =
        detail::tile_entry_cost_index<World, CostTag>(world, current);
    if (current_entry_cost == 0) {
      continue;
    }
    if (current_entry_cost > MaxCost) {
      return build_weighted_distance_field<World, PassableTag, CostTag>(
          world, goal, scratch);
    }

    const auto current_coord = detail::tile_coord<Shape>(current);
    detail::for_each_indexed_axis_neighbor<Shape>(
        current_coord, current, [&](Coord3, std::uint64_t neighbor_index) {
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
            scratch.weighted_buckets_[next_distance % bucket_count].push_back(
                neighbor_index);
            ++active_nodes;
            TESS_DIAG_EVENT(path_heap_push);
          }
        });
  }

  return DistanceFieldResult{PathStatus::Found, expanded_nodes,
                             scratch.touched_.size()};
}

template <typename World, typename PassableTag, typename CostTag>
auto weighted_distance_field_path(const World& world, Coord3 start, Coord3 goal,
                                  DistanceFieldScratch& scratch) -> PathResult {
  using Shape = typename World::shape_type;
  constexpr auto infinite_distance = std::numeric_limits<std::uint32_t>::max();

  scratch.clear_path();
  if (!contains<Shape>(start)) {
    return PathResult{PathStatus::InvalidStart, 0, 0, 0, scratch.path_};
  }
  TESS_DIAG_EVENT(path_start_passability_check);
  if (!detail::is_passable<World, PassableTag>(world, start)) {
    return PathResult{PathStatus::InvalidStart, 0, 0, 0, scratch.path_};
  }
  if (!contains<Shape>(goal)) {
    return PathResult{PathStatus::InvalidGoal, 0, 0, 0, scratch.path_};
  }
  if (!scratch.has_goal_ || scratch.goal_ != goal) {
    return PathResult{PathStatus::NoPath, 0, 0, 0, scratch.path_};
  }

  const auto start_index = detail::tile_index<Shape>(start);
  if (detail::tile_entry_cost_index<World, CostTag>(world, start_index) == 0) {
    return PathResult{PathStatus::InvalidStart, 0, 0, 0, scratch.path_};
  }

  auto current = start_index;
  auto current_distance =
      scratch.distance_at(static_cast<std::size_t>(current), infinite_distance);
  if (current_distance == infinite_distance) {
    return PathResult{PathStatus::NoPath, 0, 0, scratch.touched_.size(),
                      scratch.path_};
  }

  const auto total_cost = current_distance;
  scratch.path_.push_back(start);
  TESS_DIAG_EVENT(path_reconstruct_node);
  while (current_distance > 0) {
    const auto current_coord = detail::tile_coord<Shape>(current);
    auto next = current;
    auto next_distance = current_distance;
    detail::for_each_indexed_axis_neighbor<Shape>(
        current_coord, current, [&](Coord3, std::uint64_t neighbor_index) {
          const auto neighbor_distance = scratch.distance_at(
              static_cast<std::size_t>(neighbor_index), infinite_distance);
          if (neighbor_distance == infinite_distance ||
              neighbor_distance >= next_distance) {
            return;
          }
          const auto entry_cost = detail::tile_entry_cost_index<World, CostTag>(
              world, neighbor_index);
          if (entry_cost == 0) {
            return;
          }
          if (detail::saturating_add(neighbor_distance, entry_cost) ==
              current_distance) {
            next = neighbor_index;
            next_distance = neighbor_distance;
          }
        });

    if (next == current) {
      scratch.path_.clear();
      return PathResult{PathStatus::NoPath, 0, 0, scratch.touched_.size(),
                        scratch.path_};
    }

    current = next;
    current_distance = next_distance;
    scratch.path_.push_back(detail::tile_coord<Shape>(current));
    TESS_DIAG_EVENT(path_reconstruct_node);
  }

  return PathResult{PathStatus::Found, total_cost, scratch.path_.size(),
                    scratch.touched_.size(), scratch.path_};
}

template <typename World, typename PassableTag, typename CostTag,
          std::uint32_t MaxCost>
auto weighted_path_batch(const World& world,
                         std::span<const PathRequest> requests,
                         WeightedPathBatchScratch& scratch)
    -> std::span<const PathResult> {
  scratch.clear();
  scratch.results_.resize(requests.size());
  scratch.offsets_.assign(requests.size(), 0);
  scratch.sizes_.assign(requests.size(), 0);
  scratch.processed_.assign(requests.size(), 0);
  scratch.stats_.requests = requests.size();

  for (std::size_t i = 0; i < requests.size(); ++i) {
    if (scratch.processed_[i] != 0) {
      continue;
    }

    auto goal_count = std::size_t{0};
    for (const auto request : requests) {
      if (request.goal == requests[i].goal) {
        ++goal_count;
      }
    }
    ++scratch.stats_.unique_goals;

    if (goal_count == 1) {
      ++scratch.stats_.astar_fallbacks;
      const auto result = weighted_astar_path<World, PassableTag, CostTag>(
          world, requests[i], scratch.astar_scratch_);
      scratch.offsets_[i] = scratch.paths_.size();
      scratch.sizes_[i] = result.path.size();
      scratch.paths_.insert(scratch.paths_.end(), result.path.begin(),
                            result.path.end());
      scratch.results_[i] = PathResult{result.status,
                                       result.cost,
                                       result.expanded_nodes,
                                       result.reached_nodes,
                                       {}};
      scratch.processed_[i] = 1;
      continue;
    }

    ++scratch.stats_.field_builds;
    const auto field = build_bounded_weighted_distance_field<World, PassableTag,
                                                             CostTag, MaxCost>(
        world, requests[i].goal, scratch.field_scratch_);
    for (std::size_t j = 0; j < requests.size(); ++j) {
      if (requests[j].goal != requests[i].goal) {
        continue;
      }
      const auto result =
          field.status == PathStatus::Found
              ? weighted_distance_field_path<World, PassableTag, CostTag>(
                    world, requests[j].start, requests[j].goal,
                    scratch.field_scratch_)
              : PathResult{field.status,
                           0,
                           field.expanded_nodes,
                           field.reached_nodes,
                           {}};
      scratch.offsets_[j] = scratch.paths_.size();
      scratch.sizes_[j] = result.path.size();
      scratch.paths_.insert(scratch.paths_.end(), result.path.begin(),
                            result.path.end());
      scratch.results_[j] = PathResult{result.status,
                                       result.cost,
                                       result.expanded_nodes,
                                       result.reached_nodes,
                                       {}};
      scratch.processed_[j] = 1;
    }
  }

  for (std::size_t i = 0; i < scratch.results_.size(); ++i) {
    if (scratch.sizes_[i] == 0) {
      scratch.results_[i].path = {};
      continue;
    }
    scratch.results_[i].path = std::span<const Coord3>{
        scratch.paths_.data() + scratch.offsets_[i], scratch.sizes_[i]};
  }
  scratch.stats_.path_nodes = scratch.paths_.size();
  return scratch.results_;
}
