#pragma once

#ifndef TESS_PATH_PATH_H_INCLUDED
#error "Include <tess/path/path.h> instead of this internal detail header."
#endif

namespace detail {

template <typename World, typename Class, std::uint32_t MaxCost>
auto build_bounded_weighted_distance_field_core(
    const World& world, Coord3 goal, DistanceFieldScratch& scratch,
    [[maybe_unused]] MissingChunkPolicy policy,
    std::span<const std::uint64_t> settle_targets) -> DistanceFieldResult {
  static_assert(std::derived_from<Class, movement::movement_class_tag>,
                "build_bounded_weighted_distance_field<World, Class, MaxCost> "
                "requires a MovementClass; legacy tag pairs go through the "
                "<World, PassableTag, CostTag, MaxCost> overload.");
  static_assert(MaxCost > 0);
  using Shape = typename World::shape_type;
  using Space = detail::NodeIndexSpace<World>;
  using Model = ResolvedTransitionModel<World, Class>;
  constexpr auto infinite_distance = std::numeric_limits<std::uint32_t>::max();
  constexpr auto bucket_count = static_cast<std::size_t>(MaxCost) + 1u;

  if constexpr (Model::cost_scale != 1 ||
                !std::is_same_v<typename ShapeTraits<Shape>::lattice_type,
                                lattice::Orthogonal>) {
    return build_weighted_distance_field<World, Class>(world, goal, scratch,
                                                       policy);
  }

  TESS_DIAG_EVENT_VALUE(path_clear, scratch.touched_.size());
  scratch.clear_build();
  if (!contains<Shape>(goal)) {
    return DistanceFieldResult{PathStatus::InvalidGoal, 0, 0};
  }
  if constexpr (!Space::is_dense) {
    const Space residency{world};
    if (!residency.is_resident_index(detail::tile_index<Shape>(goal))) {
      return DistanceFieldResult{policy == MissingChunkPolicy::Indeterminate
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
    scratch.target_generation_.assign(node_count, 0);
  }
  if (scratch.weighted_buckets_.size() != bucket_count) {
    scratch.weighted_buckets_.assign(bucket_count, {});
    for (auto& bucket : scratch.weighted_buckets_) {
      bucket.reserve(scratch.weighted_bucket_capacity_);
    }
  }

  const auto goal_offset = space.offset(goal_index);
  scratch.goal_ = goal;
  scratch.has_goal_ = true;
  scratch.template stamp_model<Model>();
  scratch.stamp_residency(world);
  scratch.distance_[goal_offset] = 0;
  scratch.touch_node(goal_offset, goal_index);
  TESS_DIAG_EVENT(path_touch_node);
  scratch.weighted_buckets_[0].push_back(goal_index);
  TESS_DIAG_EVENT(path_heap_push);

  // target_generation_ is sized alongside distance_ above; settle targets
  // against a scratch whose distance_ was sized by a different builder
  // would index out of bounds.
  TESS_ASSERT(settle_targets.empty() ||
              scratch.target_generation_.size() == node_count);

  // Early termination is armed only under TreatAsBlocked: an exhausted
  // Indeterminate-policy flood must discover whether it ever skipped a
  // non-resident neighbor, and stopping early could miss that boundary
  // and misreport Indeterminate as Found.
  auto targets_remaining = std::size_t{0};
  if (Space::is_dense || policy == MissingChunkPolicy::TreatAsBlocked) {
    for (const auto target : settle_targets) {
      const auto target_offset = space.offset(target);
      if (!scratch.is_settle_target(target_offset)) {
        scratch.mark_settle_target(target_offset);
        ++targets_remaining;
      }
    }
  }

  auto active_nodes = std::size_t{1};
  auto current_distance = std::uint32_t{0};
  std::size_t expanded_nodes = 0;
  [[maybe_unused]] bool crossed_missing = false;
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

    const auto current_offset = space.offset(current);
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

    // A settled node's distance is final (Dijkstra order), so once every
    // caller-declared target has settled the rest of the reachable
    // component is unneeded work. Every node on any settled target's
    // optimal route has a strictly smaller final distance and is already
    // settled, so reconstruction never reads an unsettled node. Costs and
    // statuses are identical to a full flood; among equal-cost optimal
    // routes the truncated field's leftover tentative labels may tie-break
    // reconstruction to a different (equally optimal) tile sequence.
    if (targets_remaining != 0 && scratch.is_settle_target(current_offset)) {
      --targets_remaining;
      if (targets_remaining == 0) {
        return DistanceFieldResult{PathStatus::Found, expanded_nodes,
                                   scratch.touched_.size()};
      }
    }

    const auto current_entry_cost =
        detail::tile_entry_cost_index<World, Class>(world, current);
    if (current_entry_cost == 0) {
      continue;
    }
    if (current_entry_cost > MaxCost) {
      // Targets are dropped on this rare escape hatch: the unbounded
      // builder floods to exhaustion (correct, just not truncated).
      return build_weighted_distance_field<World, Class>(world, goal, scratch,
                                                         policy);
    }

    const auto current_coord = detail::tile_coord<Shape>(current);
    detail::for_each_indexed_axis_neighbor<Shape>(
        current_coord, current, [&](Coord3, std::uint64_t neighbor_index) {
          TESS_DIAG_EVENT(path_neighbor_candidate);
          const auto neighbor_offset = space.resident_offset(neighbor_index);
          if constexpr (!Space::is_dense) {
            if (neighbor_offset == Space::npos_offset) {
              crossed_missing = true;
              return;
            }
          }
          TESS_DIAG_EVENT(path_relax_attempt);
          if (!scratch.is_current(neighbor_offset)) {
            TESS_DIAG_EVENT(path_passability_check);
            if (!detail::is_passable_index<World, Class>(world,
                                                         neighbor_index)) {
              TESS_DIAG_EVENT(path_neighbor_blocked);
              return;
            }
            if (detail::tile_entry_cost_index<World, Class>(
                    world, neighbor_index) == 0) {
              TESS_DIAG_EVENT(path_neighbor_blocked);
              return;
            }
            scratch.distance_[neighbor_offset] = infinite_distance;
            scratch.touch_node(neighbor_offset, neighbor_index);
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

  if constexpr (!Space::is_dense) {
    if (crossed_missing && policy == MissingChunkPolicy::Indeterminate) {
      return DistanceFieldResult{PathStatus::Indeterminate, expanded_nodes,
                                 scratch.touched_.size()};
    }
  }
  return DistanceFieldResult{PathStatus::Found, expanded_nodes,
                             scratch.touched_.size()};
}

}  // namespace detail

/// Builds a weighted goal-rooted field using bounded buckets when possible.
///
/// Costs above `MaxCost` fall back to the unbounded builder. The result remains
/// in caller-owned `scratch` and may allocate unless capacity was reserved.
template <typename World, typename Class, std::uint32_t MaxCost>
auto build_bounded_weighted_distance_field(const World& world, Coord3 goal,
                                           DistanceFieldScratch& scratch,
                                           MissingChunkPolicy policy)
    -> DistanceFieldResult {
  return detail::build_bounded_weighted_distance_field_core<World, Class,
                                                            MaxCost>(
      world, goal, scratch, policy, {});
}

/// Builds a provider-aware field, conservatively using the unbounded queue.
template <typename World, typename Class, std::uint32_t MaxCost,
          typename Provider>
auto build_bounded_weighted_distance_field(const World& world, Coord3 goal,
                                           DistanceFieldScratch& scratch,
                                           MissingChunkPolicy policy,
                                           const Provider& provider)
    -> DistanceFieldResult {
  (void)MaxCost;
  return build_weighted_distance_field<World, Class, Provider>(
      world, goal, scratch, policy, provider);
}

namespace detail {

// Core of weighted_distance_field_path. verify_residency guards the
// O(resident_count) fingerprint recompute: standalone readers must verify
// (the field may be stale against this world), but weighted_path_batch
// reads each field against the same const world it just built it from, so
// it verifies once per group and skips the per-member recompute
// (audit 2026-07-11 M2).
template <typename World, typename Class, typename Provider>
auto weighted_distance_field_path_core(const World& world, Coord3 start,
                                       Coord3 goal,
                                       DistanceFieldScratch& scratch,
                                       bool verify_residency,
                                       const Provider& provider) -> PathResult {
  static_assert(std::derived_from<Class, movement::movement_class_tag>,
                "weighted_distance_field_path<World, Class> requires a "
                "MovementClass; legacy tag pairs go through the "
                "<World, PassableTag, CostTag> overload.");
  using Shape = typename World::shape_type;
  using Space = detail::NodeIndexSpace<World>;
  using Model = ResolvedTransitionModel<World, Class, Provider>;
  constexpr auto infinite_distance = std::numeric_limits<std::uint32_t>::max();
  const auto make_result = [](PathStatus status, std::uint32_t cost,
                              std::size_t expanded, std::size_t reached,
                              PathView path) {
    return PathResult{status, cost, expanded, reached, path, Model::cost_scale};
  };

  scratch.clear_path();
  if (!contains<Shape>(start)) {
    return make_result(PathStatus::InvalidStart, 0, 0, 0, scratch.path_);
  }
  if constexpr (!Space::is_dense) {
    // Pure reader: a non-resident start is not in the field (its slot would be
    // out of bounds). The field's own truncation status came from the build.
    const Space residency{world};
    if (!residency.is_resident_index(detail::tile_index<Shape>(start))) {
      return make_result(PathStatus::InvalidStart, 0, 0, 0, scratch.path_);
    }
  }
  TESS_DIAG_EVENT(path_start_passability_check);
  if (!detail::is_passable<World, Class>(world, start)) {
    return make_result(PathStatus::InvalidStart, 0, 0, 0, scratch.path_);
  }
  if (!contains<Shape>(goal)) {
    return make_result(PathStatus::InvalidGoal, 0, 0, 0, scratch.path_);
  }
  const auto model = Model{provider};
  if (!scratch.has_goal_ || scratch.goal_ != goal ||
      !scratch.template model_matches<Model>(model) ||
      (verify_residency && !scratch.residency_matches(world))) {
    return make_result(PathStatus::NoPath, 0, 0, 0, scratch.path_);
  }

  const Space space{world};
  const auto start_index = detail::tile_index<Shape>(start);
  if (detail::tile_entry_cost_index<World, Class>(world, start_index) == 0) {
    return make_result(PathStatus::InvalidStart, 0, 0, 0, scratch.path_);
  }

  auto current = start_index;
  auto current_distance =
      scratch.distance_at(space.offset(current), infinite_distance);
  if (current_distance == infinite_distance) {
    return make_result(PathStatus::NoPath, 0, 0, scratch.touched_.size(),
                       scratch.path_);
  }

  const auto total_cost = current_distance;
  scratch.path_.push_back(start);
  TESS_DIAG_EVENT(path_reconstruct_node);
  while (current_distance > 0) {
    const auto current_coord = detail::tile_coord<Shape>(current);
    auto next = current;
    auto next_distance = current_distance;
    model.for_each_forward(world, current_coord, current, [&](auto probe) {
      if (probe.availability != TransitionAvailability::Legal ||
          probe.cost_overflow) {
        return;
      }
      const auto neighbor_index = probe.to_index;
      const auto neighbor_offset = space.resident_offset(neighbor_index);
      if constexpr (!Space::is_dense) {
        if (neighbor_offset == Space::npos_offset) {
          return;
        }
      }
      const auto neighbor_distance =
          scratch.distance_at(neighbor_offset, infinite_distance);
      if (neighbor_distance == infinite_distance ||
          neighbor_distance >= next_distance) {
        return;
      }
      if (detail::saturating_add(neighbor_distance, probe.cost) ==
          current_distance) {
        next = neighbor_index;
        next_distance = neighbor_distance;
      }
    });

    if (next == current) {
      scratch.path_.clear();
      return make_result(PathStatus::NoPath, 0, 0, scratch.touched_.size(),
                         scratch.path_);
    }

    current = next;
    current_distance = next_distance;
    scratch.path_.push_back(detail::tile_coord<Shape>(current));
    TESS_DIAG_EVENT(path_reconstruct_node);
  }

  return make_result(PathStatus::Found, total_cost, scratch.path_.size(),
                     scratch.touched_.size(), scratch.path_);
}

template <typename World, typename Class>
auto weighted_distance_field_path_core(const World& world, Coord3 start,
                                       Coord3 goal,
                                       DistanceFieldScratch& scratch,
                                       bool verify_residency) -> PathResult {
  return weighted_distance_field_path_core<World, Class, AdjacentTransitions>(
      world, start, goal, scratch, verify_residency, AdjacentTransitions{});
}

}  // namespace detail

/// Reconstructs a minimum-cost path through the last matching weighted field.
///
/// The returned path borrows `scratch` until its next mutation. A mismatched
/// goal or sparse residency snapshot returns `NoPath`.
template <typename World, typename Class>
auto weighted_distance_field_path(const World& world, Coord3 start, Coord3 goal,
                                  DistanceFieldScratch& scratch) -> PathResult {
  return detail::weighted_distance_field_path_core<World, Class>(
      world, start, goal, scratch, /*verify_residency=*/true);
}

/// Reconstructs a weighted field path using the matching special provider.
template <typename World, typename Class, typename Provider>
auto weighted_distance_field_path(const World& world, Coord3 start, Coord3 goal,
                                  DistanceFieldScratch& scratch,
                                  const Provider& provider) -> PathResult {
  return detail::weighted_distance_field_path_core<World, Class, Provider>(
      world, start, goal, scratch, /*verify_residency=*/true, provider);
}

namespace detail {

// Mirrors weighted_astar_path's endpoint validation for a member of a
// shared-goal group whose field build failed: the start (containment,
// passability) is checked before any goal status, and a zero-entry-cost
// start outranks only a zero-entry-cost goal, matching the single-request
// check order exactly.
template <typename World, typename Class, typename Provider>
[[nodiscard]] auto weighted_group_member_failure(const World& world,
                                                 PathRequest request,
                                                 DistanceFieldResult field)
    -> PathResult {
  using Shape = typename World::shape_type;
  using Model = ResolvedTransitionModel<World, Class, Provider>;
  if (!contains<Shape>(request.start) ||
      !is_passable<World, Class>(world, request.start)) {
    return PathResult{PathStatus::InvalidStart, 0, 0, 0, {}, Model::cost_scale};
  }
  if (contains<Shape>(request.goal) &&
      is_passable<World, Class>(world, request.goal) &&
      tile_entry_cost_index<World, Class>(
          world, tile_index<Shape>(request.start)) == 0) {
    return PathResult{PathStatus::InvalidStart, 0, 0, 0, {}, Model::cost_scale};
  }
  return PathResult{field.status,        0,  field.expanded_nodes,
                    field.reached_nodes, {}, Model::cost_scale};
}

}  // namespace detail

/// Solves weighted requests while sharing one distance-field build per goal.
///
/// Result and path spans borrow `scratch` until mutation. Reserve request,
/// search, and path storage to avoid allocation once warm.
template <typename World, typename Class, std::uint32_t MaxCost,
          typename Provider>
auto weighted_path_batch(const World& world,
                         std::span<const PathRequest> requests,
                         WeightedPathBatchScratch& scratch,
                         const Provider& provider)
    -> std::span<const PathResult> {
  static_assert(std::derived_from<Class, movement::movement_class_tag>,
                "weighted_path_batch<World, Class, MaxCost> requires a "
                "MovementClass; legacy tag pairs go through the "
                "<World, PassableTag, CostTag, MaxCost> overload.");
  // Residency-agnostic: fans out to weighted_astar_path,
  // build_bounded_weighted_distance_field (-> build_weighted_distance_field on
  // cost overflow), weighted_distance_field_path, and
  // detail::weighted_group_member_failure, all sparse-native. Grouping is
  // Coord3/request-index space; node arrays live in the callees' scratch sized
  // by NodeIndexSpace::capacity_hint. With the default
  // MissingChunkPolicy::TreatAsBlocked a non-resident chunk reads as a wall, so
  // the batch yields NoPath (not Indeterminate) across a missing chunk; policy
  // threading is deferred.
  scratch.clear();
  scratch.results_.resize(requests.size());
  scratch.offsets_.assign(requests.size(), 0);
  scratch.sizes_.assign(requests.size(), 0);
  scratch.processed_.assign(requests.size(), 0);
  scratch.stats_.requests = requests.size();

  // Build the goal -> request count map once through a reusable
  // open-addressed flat hash (power-of-two capacity, linear probing),
  // replacing the per-request O(n) rescan.
  constexpr auto no_goal = std::numeric_limits<std::uint32_t>::max();
  scratch.goal_coords_.clear();
  scratch.goal_counts_.clear();
  scratch.request_goal_.assign(requests.size(), no_goal);
  auto slot_capacity = scratch.goal_slots_.size() < 16u
                           ? std::size_t{16}
                           : scratch.goal_slots_.size();
  while (slot_capacity < (requests.size() + 1u) * 2u) {
    slot_capacity *= 2u;
  }
  scratch.goal_slots_.assign(slot_capacity, 0u);
  const auto slot_mask = slot_capacity - 1u;
  for (std::size_t i = 0; i < requests.size(); ++i) {
    auto slot = static_cast<std::size_t>(detail::coord_hash(requests[i].goal)) &
                slot_mask;
    auto goal_index = no_goal;
    while (scratch.goal_slots_[slot] != 0u) {
      const auto candidate = scratch.goal_slots_[slot] - 1u;
      if (scratch.goal_coords_[candidate] == requests[i].goal) {
        goal_index = candidate;
        break;
      }
      slot = (slot + 1u) & slot_mask;
    }
    if (goal_index == no_goal) {
      goal_index = static_cast<std::uint32_t>(scratch.goal_coords_.size());
      scratch.goal_slots_[slot] = goal_index + 1u;
      scratch.goal_coords_.push_back(requests[i].goal);
      scratch.goal_counts_.push_back(0u);
    }
    scratch.request_goal_[i] = goal_index;
    ++scratch.goal_counts_[goal_index];
  }

  // Counting-sort the requests into per-goal member buckets so each group
  // below visits exactly its own members (the old scatter rescanned all n
  // requests once per multi-member group).
  scratch.group_offsets_.assign(scratch.goal_coords_.size() + 1u, 0u);
  for (std::size_t i = 0; i < requests.size(); ++i) {
    ++scratch.group_offsets_[scratch.request_goal_[i] + 1u];
  }
  for (std::size_t g = 1; g < scratch.group_offsets_.size(); ++g) {
    scratch.group_offsets_[g] += scratch.group_offsets_[g - 1u];
  }
  scratch.group_cursors_.assign(scratch.group_offsets_.begin(),
                                scratch.group_offsets_.end());
  scratch.group_members_.assign(requests.size(), 0u);
  for (std::size_t i = 0; i < requests.size(); ++i) {
    scratch.group_members_[scratch.group_cursors_[scratch.request_goal_[i]]++] =
        static_cast<std::uint32_t>(i);
  }

  for (std::size_t i = 0; i < requests.size(); ++i) {
    if (scratch.processed_[i] != 0) {
      continue;
    }

    const auto goal_count = static_cast<std::size_t>(
        scratch.goal_counts_[scratch.request_goal_[i]]);
    ++scratch.stats_.unique_goals;

    if (goal_count == 1) {
      ++scratch.stats_.astar_fallbacks;
      const auto result = weighted_astar_path<World, Class, Provider>(
          world, requests[i], scratch.astar_scratch_,
          MissingChunkPolicy::TreatAsBlocked, provider);
      scratch.offsets_[i] = scratch.paths_.size();
      scratch.sizes_[i] = result.path.size();
      scratch.paths_.insert(scratch.paths_.end(), result.path.begin(),
                            result.path.end());
      scratch.results_[i] =
          PathResult{result.status,        result.cost, result.expanded_nodes,
                     result.reached_nodes, {},          result.cost_scale};
      scratch.processed_[i] = 1;
      continue;
    }

    ++scratch.stats_.field_builds;
    const auto group = scratch.request_goal_[i];
    const auto members_begin = scratch.group_offsets_[group];
    const auto members_end = scratch.group_offsets_[group + 1u];

    // Hand the build every member start it will be read for (validated
    // exactly as the read path validates them, so an invalid start never
    // holds the flood open): once all of them settle, the flood stops
    // instead of exhausting the reachable component.
    using Shape = typename World::shape_type;
    scratch.settle_targets_.clear();
    for (auto member = members_begin; member < members_end; ++member) {
      const auto start = requests[scratch.group_members_[member]].start;
      if (!contains<Shape>(start)) {
        continue;
      }
      const auto start_index = detail::tile_index<Shape>(start);
      if constexpr (!detail::NodeIndexSpace<World>::is_dense) {
        const detail::NodeIndexSpace<World> residency{world};
        if (!residency.is_resident_index(start_index)) {
          continue;
        }
      }
      if (!detail::is_passable<World, Class>(world, start) ||
          detail::tile_entry_cost_index<World, Class>(world, start_index) ==
              0) {
        continue;
      }
      scratch.settle_targets_.push_back(start_index);
    }

    const auto field = [&] {
      if constexpr (std::is_same_v<Provider, AdjacentTransitions>) {
        return detail::build_bounded_weighted_distance_field_core<World, Class,
                                                                  MaxCost>(
            world, requests[i].goal, scratch.field_scratch_,
            MissingChunkPolicy::TreatAsBlocked, scratch.settle_targets_);
      } else {
        return build_weighted_distance_field<World, Class, Provider>(
            world, requests[i].goal, scratch.field_scratch_,
            MissingChunkPolicy::TreatAsBlocked, provider);
      }
    }();
    // The field was just built from this same const world, so the stamp
    // matches by construction; verify once per group (debug) instead of
    // recomputing the O(resident_count) fingerprint per member.
    TESS_ASSERT(field.status != PathStatus::Found ||
                scratch.field_scratch_.residency_matches(world));
    for (auto member = members_begin; member < members_end; ++member) {
      const auto j = static_cast<std::size_t>(scratch.group_members_[member]);
      const auto result =
          field.status == PathStatus::Found
              ? detail::weighted_distance_field_path_core<World, Class,
                                                          Provider>(
                    world, requests[j].start, requests[j].goal,
                    scratch.field_scratch_, /*verify_residency=*/false,
                    provider)
              : detail::weighted_group_member_failure<World, Class, Provider>(
                    world, requests[j], field);
      scratch.offsets_[j] = scratch.paths_.size();
      scratch.sizes_[j] = result.path.size();
      scratch.paths_.insert(scratch.paths_.end(), result.path.begin(),
                            result.path.end());
      scratch.results_[j] =
          PathResult{result.status,        result.cost, result.expanded_nodes,
                     result.reached_nodes, {},          result.cost_scale};
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

template <typename World, typename Class, std::uint32_t MaxCost>
/// Solves a bounded weighted batch without special transitions.
auto weighted_path_batch(const World& world,
                         std::span<const PathRequest> requests,
                         WeightedPathBatchScratch& scratch)
    -> std::span<const PathResult> {
  return weighted_path_batch<World, Class, MaxCost, AdjacentTransitions>(
      world, requests, scratch, AdjacentTransitions{});
}

// --- legacy <PassableTag, CostTag> forwarders
// --------------------------------- One movement class replaces the tag pair;
// LegacyWeighted preserves the historical semantics exactly, including the
// cost-agnostic passability asymmetry (the region graph may be more permissive
// than the weighted search, never the reverse).

template <typename World, typename PassableTag, typename CostTag,
          std::uint32_t MaxCost>
auto build_bounded_weighted_distance_field(const World& world, Coord3 goal,
                                           DistanceFieldScratch& scratch,
                                           MissingChunkPolicy policy)
    -> DistanceFieldResult {
  return build_bounded_weighted_distance_field<
      World, movement::LegacyWeighted<PassableTag, CostTag>, MaxCost>(
      world, goal, scratch, policy);
}

template <typename World, typename PassableTag, typename CostTag>
/// Reconstructs a weighted path using separate legacy field tags.
auto weighted_distance_field_path(const World& world, Coord3 start, Coord3 goal,
                                  DistanceFieldScratch& scratch) -> PathResult {
  return weighted_distance_field_path<
      World, movement::LegacyWeighted<PassableTag, CostTag>>(world, start, goal,
                                                             scratch);
}

template <typename World, typename PassableTag, typename CostTag,
          std::uint32_t MaxCost>
/// Solves a weighted batch using separate legacy passability and cost tags.
auto weighted_path_batch(const World& world,
                         std::span<const PathRequest> requests,
                         WeightedPathBatchScratch& scratch)
    -> std::span<const PathResult> {
  return weighted_path_batch<
      World, movement::LegacyWeighted<PassableTag, CostTag>, MaxCost>(
      world, requests, scratch);
}
