#pragma once

#ifndef TESS_PATH_PATH_H_INCLUDED
#error "Include <tess/path/path.h> instead of this internal detail header."
#endif

// A* search cores (astar_path, weighted_astar_path) extracted from path.h
// (Slice 0 pre-split) to keep path.h under the 24k-token hook. Bare detail
// fragment: included by path.h from inside namespace tess, never directly.

/// Finds a minimum-step path through tiles where field `Tag` is truthy.
///
/// The returned path borrows `scratch` until its next mutation. Sparse-world
/// resident boundaries follow `policy`.
template <typename World, typename Tag>
auto astar_path(const World& world, PathRequest request, PathScratch& scratch,
                [[maybe_unused]] MissingChunkPolicy policy) -> PathResult {
  using Shape = typename World::shape_type;
  using Space = detail::NodeIndexSpace<World>;
  using Class = movement::movement_class_of<Tag>;
  using UnitClass = movement::detail::UnitMovementClass<Class>;
  using Model = ResolvedTransitionModel<World, UnitClass>;
  constexpr auto unseen = std::uint8_t{0};
  constexpr auto open = std::uint8_t{1};
  constexpr auto closed = std::uint8_t{2};
  constexpr auto no_parent = std::numeric_limits<std::uint64_t>::max();
  constexpr auto infinite_cost = std::numeric_limits<std::uint32_t>::max();

  if constexpr (Model::cost_scale != 1) {
    return weighted_astar_path<World, UnitClass>(world, request, scratch,
                                                 policy);
  }

  TESS_DIAG_EVENT_VALUE(path_clear, scratch.touched_count_);
  scratch.clear();
  if (!contains<Shape>(request.start)) {
    return PathResult{PathStatus::InvalidStart, 0, 0, 0, scratch.path_};
  }
  if constexpr (!Space::is_dense) {
    // A non-resident start is not a definite failure: under Indeterminate the
    // search cannot rule out a route, and indexing the node arrays below with a
    // non-resident tile would go out of bounds. Resolve it before either.
    const Space residency{world};
    if (!residency.is_resident_index(
            detail::tile_index<Shape>(request.start))) {
      return PathResult{policy == MissingChunkPolicy::Indeterminate
                            ? PathStatus::Indeterminate
                            : PathStatus::InvalidStart,
                        0, 0, 0, scratch.path_};
    }
  }
  TESS_DIAG_EVENT(path_start_passability_check);
  if (!detail::is_passable<World, Tag>(world, request.start)) {
    return PathResult{PathStatus::InvalidStart, 0, 0, 0, scratch.path_};
  }
  if (!contains<Shape>(request.goal)) {
    return PathResult{PathStatus::InvalidGoal, 0, 0, 0, scratch.path_};
  }
  if constexpr (!Space::is_dense) {
    const Space residency{world};
    if (!residency.is_resident_index(detail::tile_index<Shape>(request.goal))) {
      return PathResult{policy == MissingChunkPolicy::Indeterminate
                            ? PathStatus::Indeterminate
                            : PathStatus::InvalidGoal,
                        0, 0, 0, scratch.path_};
    }
  }
  TESS_DIAG_EVENT(path_goal_passability_check);
  if (!detail::is_passable<World, Tag>(world, request.goal)) {
    return PathResult{PathStatus::InvalidGoal, 0, 0, 0, scratch.path_};
  }

  // The pre-A* fast-path scan reasons from definite passability along straight
  // lines and detours. A sparse world cannot answer definitely across a
  // non-resident chunk (a missing chunk reads as blocked), so the scan would
  // risk a false NoPath; it runs for dense worlds only. Sparse searches fall
  // straight through to the full A* below, which honors MissingChunkPolicy.
  // Dense codegen is unchanged (the guard is compiled away).
  if constexpr (Space::is_dense &&
                std::is_same_v<typename ShapeTraits<Shape>::lattice_type,
                               lattice::Orthogonal>) {
    auto direct_current = request.start;
    auto direct_blocked_by_barrier = false;
    auto direct_blocked_coord = request.start;
    auto direct_blocked_axis = detail::Axis::X;
    const auto step_direct_axis = [&](auto Coord3::* member,
                                      detail::Axis axis) {
      while (direct_current.*member != request.goal.*member) {
        direct_current.*member +=
            direct_current.*member < request.goal.*member ? 1 : -1;
        TESS_DIAG_EVENT(path_passability_check);
        if (!detail::is_passable<World, Tag>(world, direct_current)) {
          direct_blocked_coord = direct_current;
          direct_blocked_axis = axis;
          direct_blocked_by_barrier = detail::is_full_axis_barrier<World, Tag>(
              world, direct_current, axis);
          scratch.path_.clear();
          return false;
        }
        scratch.path_.push_back(direct_current);
        TESS_DIAG_EVENT(path_reconstruct_node);
      }
      return true;
    };
    const auto try_direct_order =
        [&](auto first_member, detail::Axis first_axis, auto second_member,
            detail::Axis second_axis, auto third_member,
            detail::Axis third_axis) {
          direct_current = request.start;
          scratch.path_.clear();
          scratch.path_.push_back(direct_current);
          TESS_DIAG_EVENT(path_reconstruct_node);
          return step_direct_axis(first_member, first_axis) &&
                 step_direct_axis(second_member, second_axis) &&
                 step_direct_axis(third_member, third_axis);
        };
    auto direct_path_found = false;
    if constexpr (ShapeTraits<Shape>::degenerate_z) {
      direct_path_found =
          try_direct_order(&Coord3::x, detail::Axis::X, &Coord3::y,
                           detail::Axis::Y, &Coord3::z, detail::Axis::Z) ||
          (!direct_blocked_by_barrier &&
           try_direct_order(&Coord3::y, detail::Axis::Y, &Coord3::x,
                            detail::Axis::X, &Coord3::z, detail::Axis::Z));
    } else if constexpr (ShapeTraits<Shape>::degenerate_y) {
      direct_path_found =
          try_direct_order(&Coord3::x, detail::Axis::X, &Coord3::z,
                           detail::Axis::Z, &Coord3::y, detail::Axis::Y) ||
          (!direct_blocked_by_barrier &&
           try_direct_order(&Coord3::z, detail::Axis::Z, &Coord3::x,
                            detail::Axis::X, &Coord3::y, detail::Axis::Y));
    } else if constexpr (ShapeTraits<Shape>::degenerate_x) {
      direct_path_found =
          try_direct_order(&Coord3::y, detail::Axis::Y, &Coord3::z,
                           detail::Axis::Z, &Coord3::x, detail::Axis::X) ||
          (!direct_blocked_by_barrier &&
           try_direct_order(&Coord3::z, detail::Axis::Z, &Coord3::y,
                            detail::Axis::Y, &Coord3::x, detail::Axis::X));
    } else {
      direct_path_found =
          try_direct_order(&Coord3::x, detail::Axis::X, &Coord3::y,
                           detail::Axis::Y, &Coord3::z, detail::Axis::Z) ||
          (!direct_blocked_by_barrier &&
           try_direct_order(&Coord3::y, detail::Axis::Y, &Coord3::x,
                            detail::Axis::X, &Coord3::z, detail::Axis::Z)) ||
          (!direct_blocked_by_barrier &&
           try_direct_order(&Coord3::x, detail::Axis::X, &Coord3::z,
                            detail::Axis::Z, &Coord3::y, detail::Axis::Y)) ||
          (!direct_blocked_by_barrier &&
           try_direct_order(&Coord3::z, detail::Axis::Z, &Coord3::x,
                            detail::Axis::X, &Coord3::y, detail::Axis::Y)) ||
          (!direct_blocked_by_barrier &&
           try_direct_order(&Coord3::y, detail::Axis::Y, &Coord3::z,
                            detail::Axis::Z, &Coord3::x, detail::Axis::X)) ||
          (!direct_blocked_by_barrier &&
           try_direct_order(&Coord3::z, detail::Axis::Z, &Coord3::y,
                            detail::Axis::Y, &Coord3::x, detail::Axis::X));
    }
    if (direct_path_found) {
      const auto cost = detail::manhattan(request.start, request.goal);
      return PathResult{PathStatus::Found, cost, scratch.path_.size(),
                        scratch.path_.size(), scratch.path_};
    }
    if (direct_blocked_by_barrier) {
      return PathResult{PathStatus::NoPath, 0, 0, 0, scratch.path_};
    }

    const auto coord_member = [](detail::Axis axis) -> std::int64_t Coord3::* {
      if (axis == detail::Axis::X) {
        return &Coord3::x;
      }
      if (axis == detail::Axis::Y) {
        return &Coord3::y;
      }
      return &Coord3::z;
    };
    const auto axis_extent = [](detail::Axis axis) -> std::uint64_t {
      if (axis == detail::Axis::X) {
        return ShapeTraits<Shape>::size.x;
      }
      if (axis == detail::Axis::Y) {
        return ShapeTraits<Shape>::size.y;
      }
      return ShapeTraits<Shape>::size.z;
    };
    const auto is_degenerate_axis = [](detail::Axis axis) {
      if (axis == detail::Axis::X) {
        return ShapeTraits<Shape>::degenerate_x;
      }
      if (axis == detail::Axis::Y) {
        return ShapeTraits<Shape>::degenerate_y;
      }
      return ShapeTraits<Shape>::degenerate_z;
    };
    const auto other_2d_axis = [](detail::Axis axis) {
      if constexpr (ShapeTraits<Shape>::degenerate_z) {
        return axis == detail::Axis::X ? detail::Axis::Y : detail::Axis::X;
      } else if constexpr (ShapeTraits<Shape>::degenerate_y) {
        return axis == detail::Axis::X ? detail::Axis::Z : detail::Axis::X;
      } else {
        return axis == detail::Axis::Y ? detail::Axis::Z : detail::Axis::Y;
      }
    };

    const auto append_segment_2d = [&](Coord3 from, Coord3 to,
                                       auto first_member, auto second_member) {
      const auto restore_size = scratch.path_.size();
      auto current = from;
      const auto append_axis = [&](auto Coord3::* member) {
        while (current.*member != to.*member) {
          current.*member += current.*member < to.*member ? 1 : -1;
          TESS_DIAG_EVENT(path_passability_check);
          if (!detail::is_passable<World, Tag>(world, current)) {
            scratch.path_.resize(restore_size);
            return false;
          }
          scratch.path_.push_back(current);
          TESS_DIAG_EVENT(path_reconstruct_node);
        }
        return true;
      };

      return append_axis(first_member) && append_axis(second_member);
    };
    auto plane_gap_cost = infinite_cost;
    const auto try_plane_gap_route_2d = [&]() {
      if constexpr (ShapeTraits<Shape>::degenerate_x ||
                    ShapeTraits<Shape>::degenerate_y ||
                    ShapeTraits<Shape>::degenerate_z) {
        if (is_degenerate_axis(direct_blocked_axis)) {
          return false;
        }

        const auto scan_axis = other_2d_axis(direct_blocked_axis);
        const auto blocked_member = coord_member(direct_blocked_axis);
        const auto scan_member = coord_member(scan_axis);

        auto best_gap = Coord3{0, 0, 0};
        auto best_cost = infinite_cost;
        for (std::int64_t value = 0;
             value < static_cast<std::int64_t>(axis_extent(scan_axis));
             ++value) {
          auto gap = direct_blocked_coord;
          gap.*scan_member = value;
          TESS_DIAG_EVENT(path_passability_check);
          if (!detail::is_passable_index<World, Tag>(
                  world, detail::tile_index<Shape>(gap))) {
            continue;
          }
          const auto cost = detail::manhattan(request.start, gap) +
                            detail::manhattan(gap, request.goal);
          if (cost < best_cost) {
            best_cost = cost;
            best_gap = gap;
          }
        }

        if (best_cost == infinite_cost) {
          return false;
        }

        scratch.path_.clear();
        scratch.path_.push_back(request.start);
        TESS_DIAG_EVENT(path_reconstruct_node);
        const auto first_leg = append_segment_2d(request.start, best_gap,
                                                 blocked_member, scan_member) ||
                               append_segment_2d(request.start, best_gap,
                                                 scan_member, blocked_member);
        if (!first_leg) {
          scratch.path_.clear();
          return false;
        }
        const auto second_leg =
            append_segment_2d(best_gap, request.goal, blocked_member,
                              scan_member) ||
            append_segment_2d(best_gap, request.goal, scan_member,
                              blocked_member);
        if (!second_leg) {
          scratch.path_.clear();
          return false;
        }
        plane_gap_cost = best_cost;
        return true;
      } else {
        return false;
      }
    };
    if (try_plane_gap_route_2d()) {
      return PathResult{PathStatus::Found, plane_gap_cost, scratch.path_.size(),
                        scratch.path_.size(), scratch.path_};
    }

    const auto append_segment_3d = [&](Coord3 from, Coord3 to,
                                       auto first_member, auto second_member,
                                       auto third_member) {
      const auto restore_size = scratch.path_.size();
      auto current = from;
      const auto append_axis = [&](auto Coord3::* member) {
        while (current.*member != to.*member) {
          current.*member += current.*member < to.*member ? 1 : -1;
          TESS_DIAG_EVENT(path_passability_check);
          if (!detail::is_passable<World, Tag>(world, current)) {
            scratch.path_.resize(restore_size);
            return false;
          }
          scratch.path_.push_back(current);
          TESS_DIAG_EVENT(path_reconstruct_node);
        }
        return true;
      };

      return append_axis(first_member) && append_axis(second_member) &&
             append_axis(third_member);
    };
    const auto append_any_segment_3d = [&](Coord3 from, Coord3 to) {
      return append_segment_3d(from, to, &Coord3::x, &Coord3::y, &Coord3::z) ||
             append_segment_3d(from, to, &Coord3::x, &Coord3::z, &Coord3::y) ||
             append_segment_3d(from, to, &Coord3::y, &Coord3::x, &Coord3::z) ||
             append_segment_3d(from, to, &Coord3::y, &Coord3::z, &Coord3::x) ||
             append_segment_3d(from, to, &Coord3::z, &Coord3::x, &Coord3::y) ||
             append_segment_3d(from, to, &Coord3::z, &Coord3::y, &Coord3::x);
    };
    auto plane_gap_cost_3d = infinite_cost;
    const auto try_plane_gap_route_3d = [&]() {
      if constexpr (!(ShapeTraits<Shape>::degenerate_x ||
                      ShapeTraits<Shape>::degenerate_y ||
                      ShapeTraits<Shape>::degenerate_z)) {
        auto first_scan_axis = detail::Axis::Y;
        auto second_scan_axis = detail::Axis::Z;
        if (direct_blocked_axis == detail::Axis::Y) {
          first_scan_axis = detail::Axis::X;
          second_scan_axis = detail::Axis::Z;
        } else if (direct_blocked_axis == detail::Axis::Z) {
          first_scan_axis = detail::Axis::X;
          second_scan_axis = detail::Axis::Y;
        }
        const auto first_scan_member = coord_member(first_scan_axis);
        const auto second_scan_member = coord_member(second_scan_axis);

        auto best_gap = Coord3{0, 0, 0};
        auto best_cost = infinite_cost;
        for (std::int64_t first_value = 0;
             first_value <
             static_cast<std::int64_t>(axis_extent(first_scan_axis));
             ++first_value) {
          for (std::int64_t second_value = 0;
               second_value <
               static_cast<std::int64_t>(axis_extent(second_scan_axis));
               ++second_value) {
            auto gap = direct_blocked_coord;
            gap.*first_scan_member = first_value;
            gap.*second_scan_member = second_value;
            TESS_DIAG_EVENT(path_passability_check);
            if (!detail::is_passable_index<World, Tag>(
                    world, detail::tile_index<Shape>(gap))) {
              continue;
            }
            const auto cost = detail::manhattan(request.start, gap) +
                              detail::manhattan(gap, request.goal);
            if (cost < best_cost) {
              best_cost = cost;
              best_gap = gap;
            }
          }
        }

        if (best_cost == infinite_cost) {
          return false;
        }

        scratch.path_.clear();
        scratch.path_.push_back(request.start);
        TESS_DIAG_EVENT(path_reconstruct_node);
        if (!append_any_segment_3d(request.start, best_gap)) {
          scratch.path_.clear();
          return false;
        }
        if (!append_any_segment_3d(best_gap, request.goal)) {
          scratch.path_.clear();
          return false;
        }
        plane_gap_cost_3d = best_cost;
        return true;
      } else {
        return false;
      }
    };
    if (try_plane_gap_route_3d()) {
      return PathResult{PathStatus::Found, plane_gap_cost_3d,
                        scratch.path_.size(), scratch.path_.size(),
                        scratch.path_};
    }

    auto forced_plane_gap_cost = infinite_cost;
    auto forced_plane_gap_no_path = false;
    const auto try_forced_plane_gaps_2d = [&](detail::Axis progress_axis) {
      static_cast<void>(progress_axis);
      if constexpr (ShapeTraits<Shape>::degenerate_x ||
                    ShapeTraits<Shape>::degenerate_y ||
                    ShapeTraits<Shape>::degenerate_z) {
        if (is_degenerate_axis(progress_axis)) {
          return false;
        }

        const auto progress_member = coord_member(progress_axis);
        const auto gap_axis = other_2d_axis(progress_axis);
        const auto gap_member = coord_member(gap_axis);
        if (request.start.*progress_member == request.goal.*progress_member) {
          return false;
        }

        const auto progress_step =
            request.start.*progress_member < request.goal.*progress_member ? 1
                                                                           : -1;
        auto current = request.start;
        auto found_forced_gap = false;

        scratch.path_.clear();
        scratch.path_.push_back(current);
        TESS_DIAG_EVENT(path_reconstruct_node);

        const auto append_checked = [&](Coord3 next) {
          TESS_DIAG_EVENT(path_passability_check);
          if (!detail::is_passable<World, Tag>(world, next)) {
            scratch.path_.clear();
            return false;
          }
          scratch.path_.push_back(next);
          TESS_DIAG_EVENT(path_reconstruct_node);
          current = next;
          return true;
        };
        const auto append_progress = [&](std::int64_t target) {
          while (current.*progress_member != target) {
            auto next = current;
            next.*progress_member += current.*progress_member < target ? 1 : -1;
            if (!append_checked(next)) {
              return false;
            }
          }
          return true;
        };
        const auto append_gap = [&](std::int64_t target) {
          while (current.*gap_member != target) {
            auto next = current;
            next.*gap_member += current.*gap_member < target ? 1 : -1;
            if (!append_checked(next)) {
              return false;
            }
          }
          return true;
        };

        while (current.*progress_member != request.goal.*progress_member) {
          auto next = current;
          next.*progress_member += progress_step;
          TESS_DIAG_EVENT(path_passability_check);
          if (detail::is_passable<World, Tag>(world, next)) {
            scratch.path_.push_back(next);
            TESS_DIAG_EVENT(path_reconstruct_node);
            current = next;
            continue;
          }

          auto passable_count = std::uint64_t{0};
          auto gap_value = std::int64_t{0};
          auto blocked_count = std::uint64_t{0};
          for (std::int64_t value = 0;
               value < static_cast<std::int64_t>(axis_extent(gap_axis));
               ++value) {
            auto coord = next;
            coord.*gap_member = value;
            TESS_DIAG_EVENT(path_passability_check);
            if (detail::is_passable_index<World, Tag>(
                    world, detail::tile_index<Shape>(coord))) {
              ++passable_count;
              gap_value = value;
            } else {
              ++blocked_count;
            }
          }

          if (passable_count == 0) {
            forced_plane_gap_no_path = true;
            scratch.path_.clear();
            return false;
          }
          if (blocked_count == 0) {
            continue;
          }
          if (passable_count > 1) {
            scratch.path_.clear();
            return false;
          }

          found_forced_gap = true;
          auto gap = next;
          gap.*gap_member = gap_value;
          if (!append_gap(gap.*gap_member) ||
              !append_progress(gap.*progress_member)) {
            return false;
          }
        }

        if (!found_forced_gap) {
          scratch.path_.clear();
          return false;
        }
        if (!append_progress(request.goal.*progress_member) ||
            !append_gap(request.goal.*gap_member)) {
          return false;
        }

        forced_plane_gap_cost =
            static_cast<std::uint32_t>(scratch.path_.size() - 1);
        return true;
      } else {
        return false;
      }
    };
    if (try_forced_plane_gaps_2d(detail::Axis::X) ||
        try_forced_plane_gaps_2d(detail::Axis::Y) ||
        try_forced_plane_gaps_2d(detail::Axis::Z)) {
      return PathResult{PathStatus::Found, forced_plane_gap_cost,
                        scratch.path_.size(), scratch.path_.size(),
                        scratch.path_};
    }
    if (forced_plane_gap_no_path) {
      return PathResult{PathStatus::NoPath, 0, 0, 0, scratch.path_};
    }

    const auto try_axis_aligned_detour = [&](auto Coord3::* primary,
                                             auto Coord3::* detour,
                                             std::int64_t detour_step) {
      direct_current = request.start;
      direct_current.*detour += detour_step;
      if (!contains<Shape>(direct_current)) {
        return false;
      }
      TESS_DIAG_EVENT(path_passability_check);
      if (!detail::is_passable<World, Tag>(world, direct_current)) {
        return false;
      }

      scratch.path_.clear();
      scratch.path_.push_back(request.start);
      TESS_DIAG_EVENT(path_reconstruct_node);
      scratch.path_.push_back(direct_current);
      TESS_DIAG_EVENT(path_reconstruct_node);

      while (direct_current.*primary != request.goal.*primary) {
        direct_current.*primary +=
            direct_current.*primary < request.goal.*primary ? 1 : -1;
        TESS_DIAG_EVENT(path_passability_check);
        if (!detail::is_passable<World, Tag>(world, direct_current)) {
          scratch.path_.clear();
          return false;
        }
        scratch.path_.push_back(direct_current);
        TESS_DIAG_EVENT(path_reconstruct_node);
      }

      direct_current.*detour -= detour_step;
      TESS_DIAG_EVENT(path_passability_check);
      if (!detail::is_passable<World, Tag>(world, direct_current)) {
        scratch.path_.clear();
        return false;
      }
      scratch.path_.push_back(direct_current);
      TESS_DIAG_EVENT(path_reconstruct_node);
      return true;
    };
    const auto detour_cost = detail::manhattan(request.start, request.goal) + 2;
    if (request.start.y == request.goal.y &&
        request.start.z == request.goal.z &&
        (try_axis_aligned_detour(&Coord3::x, &Coord3::y, 1) ||
         try_axis_aligned_detour(&Coord3::x, &Coord3::y, -1) ||
         try_axis_aligned_detour(&Coord3::x, &Coord3::z, 1) ||
         try_axis_aligned_detour(&Coord3::x, &Coord3::z, -1))) {
      return PathResult{PathStatus::Found, detour_cost, scratch.path_.size(),
                        scratch.path_.size(), scratch.path_};
    }
    if (request.start.x == request.goal.x &&
        request.start.z == request.goal.z &&
        (try_axis_aligned_detour(&Coord3::y, &Coord3::x, 1) ||
         try_axis_aligned_detour(&Coord3::y, &Coord3::x, -1) ||
         try_axis_aligned_detour(&Coord3::y, &Coord3::z, 1) ||
         try_axis_aligned_detour(&Coord3::y, &Coord3::z, -1))) {
      return PathResult{PathStatus::Found, detour_cost, scratch.path_.size(),
                        scratch.path_.size(), scratch.path_};
    }
    if (request.start.x == request.goal.x &&
        request.start.y == request.goal.y &&
        (try_axis_aligned_detour(&Coord3::z, &Coord3::x, 1) ||
         try_axis_aligned_detour(&Coord3::z, &Coord3::x, -1) ||
         try_axis_aligned_detour(&Coord3::z, &Coord3::y, 1) ||
         try_axis_aligned_detour(&Coord3::z, &Coord3::y, -1))) {
      return PathResult{PathStatus::Found, detour_cost, scratch.path_.size(),
                        scratch.path_.size(), scratch.path_};
    }
  }  // if constexpr (Space::is_dense)

  const detail::NodeIndexSpace<World> space{world};
  const auto node_count = space.capacity_hint();
  if (scratch.state_.size() != node_count) {
    TESS_DIAG_EVENT(path_initialize);
    scratch.generation_.assign(node_count, 0);
    scratch.state_.assign(node_count, unseen);
    scratch.g_.assign(node_count, infinite_cost);
    scratch.parent_.assign(node_count, no_parent);
  }

  const auto start = detail::tile_index<Shape>(request.start);
  const auto goal = detail::tile_index<Shape>(request.goal);
  const auto start_offset = space.offset(start);
  scratch.g_[start_offset] = 0;
  scratch.state_[start_offset] = open;
  scratch.touch_node(start_offset);
  TESS_DIAG_EVENT(path_touch_node);
  TESS_DIAG_EVENT(path_heuristic);
  const auto model = Model{};
  auto current_f = model.heuristic(world, request.start, request.goal);
  scratch.open_.push_back(PathScratch::OpenNode{
      start,
      0,
      current_f,
  });
  TESS_DIAG_EVENT(path_heap_push);

  std::size_t expanded_nodes = 0;
  // Sparse worlds: set when the search skips a neighbor because its chunk is
  // not resident, so an exhausted search can return Indeterminate rather than
  // a NoPath it cannot justify. Never written for dense worlds (the guard that
  // sets it is compiled away), so dense still reports NoPath on exhaustion.
  [[maybe_unused]] bool crossed_missing = false;
  while (!scratch.open_.empty() || !scratch.open_next_.empty()) {
    if (scratch.open_.empty()) {
      // Saturating, mirroring the weighted core: a wrapped f would
      // mis-partition the two-bucket dial and let the goal close with a
      // non-optimal g (audit 2026-07-11 C3; needs ~2^32-tile distances,
      // not constructible today, guarded for symmetry).
      current_f = detail::saturating_add(current_f, 2);
      scratch.open_.swap(scratch.open_next_);
    }

    TESS_DIAG_EVENT(path_heap_pop);
    const auto current = scratch.open_.back();
    scratch.open_.pop_back();

    const auto current_offset = space.offset(current.index);
    const auto current_state = scratch.state_at(current_offset, unseen);
    if (current_state == closed ||
        current.g != scratch.g_at(current_offset, infinite_cost)) {
      TESS_DIAG_EVENT_VALUE(path_skip_pop, current_state == closed);
      continue;
    }
    scratch.state_[current_offset] = closed;
    ++expanded_nodes;

    if (current.index == goal) {
      auto step = current.index;
      while (true) {
        scratch.path_.push_back(detail::tile_coord<Shape>(step));
        TESS_DIAG_EVENT(path_reconstruct_node);
        if (step == start) {
          break;
        }
        step = scratch.parent_[space.offset(step)];
      }
      std::reverse(scratch.path_.begin(), scratch.path_.end());
      return PathResult{PathStatus::Found, current.g, expanded_nodes,
                        scratch.touched_count_, scratch.path_};
    }

    const auto current_coord = detail::tile_coord<Shape>(current.index);
    model.for_each_forward(
        world, current_coord, current.index, [&](auto probe) {
          TESS_DIAG_EVENT(path_neighbor_candidate);
          if (probe.availability == TransitionAvailability::MissingTopology) {
            crossed_missing = true;
            return;
          }
          const auto neighbor = probe.to;
          const auto neighbor_index = probe.to_index;
          // Combined residency+offset probe: a non-resident neighbor has no
          // node-array slot. Remember the boundary and skip it; whether that
          // means "blocked" or "unknown" is decided at exhaustion.
          const auto neighbor_offset = space.resident_offset(neighbor_index);
          if constexpr (!Space::is_dense) {
            if (neighbor_offset == Space::npos_offset) {
              crossed_missing = true;
              return;
            }
          }
          const auto neighbor_state = scratch.state_at(neighbor_offset, unseen);
          if (neighbor_state == closed) {
            TESS_DIAG_EVENT(path_neighbor_closed);
            return;
          }
          const auto tentative_g = current.g + 1;
          TESS_DIAG_EVENT(path_relax_attempt);
          if (tentative_g < scratch.g_at(neighbor_offset, infinite_cost)) {
            TESS_DIAG_EVENT(path_relax_success);
            if (neighbor_state == unseen) {
              scratch.touch_node(neighbor_offset);
              TESS_DIAG_EVENT(path_touch_node);
            }
            scratch.g_[neighbor_offset] = tentative_g;
            scratch.parent_[neighbor_offset] = current.index;
            scratch.state_[neighbor_offset] = open;
            TESS_DIAG_EVENT(path_heuristic);
            const auto updated_node = PathScratch::OpenNode{
                neighbor_index,
                tentative_g,
                detail::saturating_add(
                    tentative_g,
                    model.heuristic(world, neighbor, request.goal)),
            };
            if (updated_node.f <= current_f) {
              scratch.open_.push_back(updated_node);
            } else {
              scratch.open_next_.push_back(updated_node);
            }
            TESS_DIAG_EVENT(path_heap_push);
          }
        });
  }

  if constexpr (!Space::is_dense) {
    if (crossed_missing && policy == MissingChunkPolicy::Indeterminate) {
      return PathResult{PathStatus::Indeterminate, 0, expanded_nodes,
                        scratch.touched_count_, scratch.path_};
    }
  }
  return PathResult{PathStatus::NoPath, 0, expanded_nodes,
                    scratch.touched_count_, scratch.path_};
}

/// Finds a provider-aware minimum-step path through a passability class/tag.
template <typename World, typename Tag, typename Provider>
auto astar_path(const World& world, PathRequest request, PathScratch& scratch,
                MissingChunkPolicy policy, const Provider& provider)
    -> PathResult {
  using Class = movement::movement_class_of<Tag>;
  using UnitClass = movement::detail::UnitMovementClass<Class>;
  return weighted_astar_path<World, UnitClass, Provider>(
      world, request, scratch, policy, provider);
}

/// Finds a minimum-cost path using one compile-time movement class.
///
/// Zero entry cost is impassable. The returned path borrows `scratch` until
/// mutation, and sparse-world resident boundaries follow `policy`.
template <typename World, typename Class, typename Provider>
auto weighted_astar_path(const World& world, PathRequest request,
                         PathScratch& scratch,
                         [[maybe_unused]] MissingChunkPolicy policy,
                         const Provider& provider) -> PathResult {
  static_assert(std::derived_from<Class, movement::movement_class_tag>,
                "weighted_astar_path<World, Class> requires a MovementClass; "
                "legacy tag pairs go through the <World, PassableTag, CostTag> "
                "overload.");
  using Shape = typename World::shape_type;
  using Space = detail::NodeIndexSpace<World>;
  using Model = ResolvedTransitionModel<World, Class, Provider>;
  constexpr auto unseen = std::uint8_t{0};
  constexpr auto open = std::uint8_t{1};
  constexpr auto closed = std::uint8_t{2};
  constexpr auto no_parent = std::numeric_limits<std::uint64_t>::max();
  constexpr auto infinite_cost = std::numeric_limits<std::uint32_t>::max();
  const auto make_result = [](PathStatus status, std::uint32_t cost,
                              std::size_t expanded, std::size_t reached,
                              PathView path) {
    return PathResult{status, cost, expanded, reached, path, Model::cost_scale};
  };

  TESS_DIAG_EVENT_VALUE(path_clear, scratch.touched_count_);
  scratch.clear();
  if (!contains<Shape>(request.start)) {
    return make_result(PathStatus::InvalidStart, 0, 0, 0, scratch.path_);
  }
  if constexpr (!Space::is_dense) {
    // A non-resident start is not a definite failure: under Indeterminate the
    // search cannot rule out a route, and indexing the node arrays below with a
    // non-resident tile would go out of bounds. Resolve it before either.
    const Space residency{world};
    if (!residency.is_resident_index(
            detail::tile_index<Shape>(request.start))) {
      return PathResult{policy == MissingChunkPolicy::Indeterminate
                            ? PathStatus::Indeterminate
                            : PathStatus::InvalidStart,
                        0,
                        0,
                        0,
                        scratch.path_,
                        Model::cost_scale};
    }
  }
  TESS_DIAG_EVENT(path_start_passability_check);
  if (!detail::is_passable<World, Class>(world, request.start)) {
    return make_result(PathStatus::InvalidStart, 0, 0, 0, scratch.path_);
  }
  if (!contains<Shape>(request.goal)) {
    return make_result(PathStatus::InvalidGoal, 0, 0, 0, scratch.path_);
  }
  if constexpr (!Space::is_dense) {
    const Space residency{world};
    if (!residency.is_resident_index(detail::tile_index<Shape>(request.goal))) {
      return PathResult{policy == MissingChunkPolicy::Indeterminate
                            ? PathStatus::Indeterminate
                            : PathStatus::InvalidGoal,
                        0,
                        0,
                        0,
                        scratch.path_,
                        Model::cost_scale};
    }
  }
  TESS_DIAG_EVENT(path_goal_passability_check);
  if (!detail::is_passable<World, Class>(world, request.goal)) {
    return make_result(PathStatus::InvalidGoal, 0, 0, 0, scratch.path_);
  }

  const auto start = detail::tile_index<Shape>(request.start);
  const auto goal = detail::tile_index<Shape>(request.goal);
  if (detail::tile_entry_cost_index<World, Class>(world, start) == 0) {
    return make_result(PathStatus::InvalidStart, 0, 0, 0, scratch.path_);
  }
  if (detail::tile_entry_cost_index<World, Class>(world, goal) == 0) {
    return make_result(PathStatus::InvalidGoal, 0, 0, 0, scratch.path_);
  }

  // The pre-A* fast-path scan reasons from definite passability along straight
  // lines and detours. A sparse world cannot answer definitely across a
  // non-resident chunk (a missing chunk reads as blocked), so the scan would
  // risk a false NoPath; it runs for dense worlds only. Sparse searches fall
  // straight through to the full A* below, which honors MissingChunkPolicy.
  // Dense codegen is unchanged (the guard is compiled away).
  if constexpr (Space::is_dense && !Model::has_special_transitions &&
                std::is_same_v<typename Model::step_policy,
                               movement::DefaultSteps> &&
                std::is_same_v<typename ShapeTraits<Shape>::lattice_type,
                               lattice::Orthogonal>) {
    auto direct_current = request.start;
    auto direct_axis_blocked = false;
    const auto append_unit_axis = [&](auto Coord3::* member) {
      while (direct_current.*member != request.goal.*member) {
        direct_current.*member +=
            direct_current.*member < request.goal.*member ? 1 : -1;
        const auto direct_index = detail::tile_index<Shape>(direct_current);
        TESS_DIAG_EVENT(path_passability_check);
        if (!detail::is_passable_index<World, Class>(world, direct_index)) {
          direct_axis_blocked = true;
          scratch.path_.clear();
          return false;
        }
        const auto entry_cost =
            detail::tile_entry_cost_index<World, Class>(world, direct_index);
        if (entry_cost == 0) {
          direct_axis_blocked = true;
          scratch.path_.clear();
          return false;
        }
        if (entry_cost != 1) {
          scratch.path_.clear();
          return false;
        }
        scratch.path_.push_back(direct_current);
        TESS_DIAG_EVENT(path_reconstruct_node);
      }
      return true;
    };
    const auto try_unit_direct_order =
        [&](auto first_member, auto second_member, auto third_member) {
          direct_current = request.start;
          scratch.path_.clear();
          scratch.path_.push_back(direct_current);
          TESS_DIAG_EVENT(path_reconstruct_node);
          return append_unit_axis(first_member) &&
                 append_unit_axis(second_member) &&
                 append_unit_axis(third_member);
        };
    auto direct_path_found = false;
    if constexpr (ShapeTraits<Shape>::degenerate_z) {
      direct_path_found =
          try_unit_direct_order(&Coord3::x, &Coord3::y, &Coord3::z) ||
          try_unit_direct_order(&Coord3::y, &Coord3::x, &Coord3::z);
    } else if constexpr (ShapeTraits<Shape>::degenerate_y) {
      direct_path_found =
          try_unit_direct_order(&Coord3::x, &Coord3::z, &Coord3::y) ||
          try_unit_direct_order(&Coord3::z, &Coord3::x, &Coord3::y);
    } else if constexpr (ShapeTraits<Shape>::degenerate_x) {
      direct_path_found =
          try_unit_direct_order(&Coord3::y, &Coord3::z, &Coord3::x) ||
          try_unit_direct_order(&Coord3::z, &Coord3::y, &Coord3::x);
    } else {
      direct_path_found =
          try_unit_direct_order(&Coord3::x, &Coord3::y, &Coord3::z) ||
          try_unit_direct_order(&Coord3::x, &Coord3::z, &Coord3::y) ||
          try_unit_direct_order(&Coord3::y, &Coord3::x, &Coord3::z) ||
          try_unit_direct_order(&Coord3::y, &Coord3::z, &Coord3::x) ||
          try_unit_direct_order(&Coord3::z, &Coord3::x, &Coord3::y) ||
          try_unit_direct_order(&Coord3::z, &Coord3::y, &Coord3::x);
    }
    if (direct_path_found) {
      const auto cost = detail::manhattan(request.start, request.goal);
      return make_result(PathStatus::Found, cost, scratch.path_.size(),
                         scratch.path_.size(), scratch.path_);
    }

    const auto append_unit_detour_axis = [&](auto Coord3::* primary,
                                             auto Coord3::* detour,
                                             std::int64_t detour_step) {
      if (!direct_axis_blocked) {
        return false;
      }
      direct_current = request.start;
      direct_current.*detour += detour_step;
      if (!contains<Shape>(direct_current)) {
        return false;
      }
      const auto append_checked = [&](Coord3 coord) {
        const auto index = detail::tile_index<Shape>(coord);
        TESS_DIAG_EVENT(path_passability_check);
        if (!detail::is_passable_index<World, Class>(world, index)) {
          scratch.path_.clear();
          return false;
        }
        if (detail::tile_entry_cost_index<World, Class>(world, index) != 1) {
          scratch.path_.clear();
          return false;
        }
        scratch.path_.push_back(coord);
        TESS_DIAG_EVENT(path_reconstruct_node);
        return true;
      };

      scratch.path_.clear();
      scratch.path_.push_back(request.start);
      TESS_DIAG_EVENT(path_reconstruct_node);
      if (!append_checked(direct_current)) {
        return false;
      }

      while (direct_current.*primary != request.goal.*primary) {
        direct_current.*primary +=
            direct_current.*primary < request.goal.*primary ? 1 : -1;
        if (!append_checked(direct_current)) {
          return false;
        }
      }

      direct_current.*detour -= detour_step;
      if (!append_checked(direct_current)) {
        return false;
      }
      return true;
    };
    const auto detour_cost = detail::manhattan(request.start, request.goal) + 2;
    if (request.start.y == request.goal.y &&
        request.start.z == request.goal.z &&
        (append_unit_detour_axis(&Coord3::x, &Coord3::y, 1) ||
         append_unit_detour_axis(&Coord3::x, &Coord3::y, -1) ||
         append_unit_detour_axis(&Coord3::x, &Coord3::z, 1) ||
         append_unit_detour_axis(&Coord3::x, &Coord3::z, -1))) {
      return make_result(PathStatus::Found, detour_cost, scratch.path_.size(),
                         scratch.path_.size(), scratch.path_);
    }
    if (request.start.x == request.goal.x &&
        request.start.z == request.goal.z &&
        (append_unit_detour_axis(&Coord3::y, &Coord3::x, 1) ||
         append_unit_detour_axis(&Coord3::y, &Coord3::x, -1) ||
         append_unit_detour_axis(&Coord3::y, &Coord3::z, 1) ||
         append_unit_detour_axis(&Coord3::y, &Coord3::z, -1))) {
      return make_result(PathStatus::Found, detour_cost, scratch.path_.size(),
                         scratch.path_.size(), scratch.path_);
    }
    if (request.start.x == request.goal.x &&
        request.start.y == request.goal.y &&
        (append_unit_detour_axis(&Coord3::z, &Coord3::x, 1) ||
         append_unit_detour_axis(&Coord3::z, &Coord3::x, -1) ||
         append_unit_detour_axis(&Coord3::z, &Coord3::y, 1) ||
         append_unit_detour_axis(&Coord3::z, &Coord3::y, -1))) {
      return make_result(PathStatus::Found, detour_cost, scratch.path_.size(),
                         scratch.path_.size(), scratch.path_);
    }
  }  // if constexpr (Space::is_dense)

  const detail::NodeIndexSpace<World> space{world};
  const auto node_count = space.capacity_hint();
  if (scratch.state_.size() != node_count) {
    TESS_DIAG_EVENT(path_initialize);
    scratch.generation_.assign(node_count, 0);
    scratch.state_.assign(node_count, unseen);
    scratch.g_.assign(node_count, infinite_cost);
    scratch.parent_.assign(node_count, no_parent);
  }

  const auto start_offset = space.offset(start);
  scratch.g_[start_offset] = 0;
  scratch.state_[start_offset] = open;
  scratch.touch_node(start_offset);
  TESS_DIAG_EVENT(path_touch_node);
  TESS_DIAG_EVENT(path_heuristic);
  const auto model = Model{provider};
  scratch.open_.push_back(PathScratch::OpenNode{
      start,
      0,
      model.heuristic(world, request.start, request.goal),
  });
  std::push_heap(scratch.open_.begin(), scratch.open_.end(),
                 detail::open_node_less);
  TESS_DIAG_EVENT(path_heap_push);

  std::size_t expanded_nodes = 0;
  // Sparse worlds: set when the search skips a neighbor because its chunk is
  // not resident, so an exhausted search can return Indeterminate rather than
  // a NoPath it cannot justify. Never written for dense worlds (the guard that
  // sets it is compiled away), so dense still reports NoPath on exhaustion.
  [[maybe_unused]] bool crossed_missing = false;
  auto cost_overflow = false;
  while (!scratch.open_.empty()) {
    TESS_DIAG_EVENT(path_heap_pop);
    std::pop_heap(scratch.open_.begin(), scratch.open_.end(),
                  detail::open_node_less);
    const auto current = scratch.open_.back();
    scratch.open_.pop_back();

    const auto current_offset = space.offset(current.index);
    const auto current_state = scratch.state_at(current_offset, unseen);
    if (current_state == closed ||
        current.g != scratch.g_at(current_offset, infinite_cost)) {
      TESS_DIAG_EVENT_VALUE(path_skip_pop, current_state == closed);
      continue;
    }
    scratch.state_[current_offset] = closed;
    ++expanded_nodes;

    if (current.index == goal) {
      auto step = current.index;
      while (true) {
        scratch.path_.push_back(detail::tile_coord<Shape>(step));
        TESS_DIAG_EVENT(path_reconstruct_node);
        if (step == start) {
          break;
        }
        step = scratch.parent_[space.offset(step)];
      }
      std::reverse(scratch.path_.begin(), scratch.path_.end());
      return make_result(PathStatus::Found, current.g, expanded_nodes,
                         scratch.touched_count_, scratch.path_);
    }

    const auto current_coord = detail::tile_coord<Shape>(current.index);
    model.for_each_forward(
        world, current_coord, current.index, [&](auto probe) {
          TESS_DIAG_EVENT(path_neighbor_candidate);
          if (probe.availability == TransitionAvailability::MissingTopology) {
            crossed_missing = true;
            return;
          }
          if (probe.cost_overflow) {
            cost_overflow = true;
            return;
          }
          const auto neighbor = probe.to;
          const auto neighbor_index = probe.to_index;
          // Combined residency+offset probe: a non-resident neighbor has no
          // node-array slot. Remember the boundary and skip it; whether that
          // means "blocked" or "unknown" is decided at exhaustion.
          const auto neighbor_offset = space.resident_offset(neighbor_index);
          if constexpr (!Space::is_dense) {
            if (neighbor_offset == Space::npos_offset) {
              crossed_missing = true;
              return;
            }
          }
          const auto neighbor_state = scratch.state_at(neighbor_offset, unseen);
          if (neighbor_state == closed) {
            TESS_DIAG_EVENT(path_neighbor_closed);
            return;
          }
          TESS_DIAG_EVENT(path_relax_attempt);
          const auto tentative_g =
              detail::saturating_add(current.g, probe.cost);
          if (tentative_g == infinite_cost) {
            cost_overflow = true;
            return;
          }
          if (tentative_g < scratch.g_at(neighbor_offset, infinite_cost)) {
            TESS_DIAG_EVENT(path_relax_success);
            if (neighbor_state == unseen) {
              scratch.touch_node(neighbor_offset);
              TESS_DIAG_EVENT(path_touch_node);
            }
            scratch.g_[neighbor_offset] = tentative_g;
            scratch.parent_[neighbor_offset] = current.index;
            scratch.state_[neighbor_offset] = open;
            TESS_DIAG_EVENT(path_heuristic);
            scratch.open_.push_back(PathScratch::OpenNode{
                neighbor_index,
                tentative_g,
                detail::saturating_add(
                    tentative_g,
                    model.heuristic(world, neighbor, request.goal)),
            });
            std::push_heap(scratch.open_.begin(), scratch.open_.end(),
                           detail::open_node_less);
            TESS_DIAG_EVENT(path_heap_push);
          }
        });
  }

  if constexpr (!Space::is_dense) {
    if (crossed_missing && policy == MissingChunkPolicy::Indeterminate) {
      return make_result(PathStatus::Indeterminate, 0, expanded_nodes,
                         scratch.touched_count_, scratch.path_);
    }
  }
  return make_result(
      cost_overflow ? PathStatus::CostOverflow : PathStatus::NoPath, 0,
      expanded_nodes, scratch.touched_count_, scratch.path_);
}

template <typename World, typename Class>
auto weighted_astar_path(const World& world, PathRequest request,
                         PathScratch& scratch, MissingChunkPolicy policy)
    -> PathResult {
  return weighted_astar_path<World, Class, AdjacentTransitions>(
      world, request, scratch, policy, AdjacentTransitions{});
}

// Legacy <PassableTag, CostTag> forwarder: one movement class replaces the
// tag pair; LegacyWeighted preserves the historical semantics exactly,
// including the cost-agnostic passability asymmetry.
template <typename World, typename PassableTag, typename CostTag>
auto weighted_astar_path(const World& world, PathRequest request,
                         PathScratch& scratch, MissingChunkPolicy policy)
    -> PathResult {
  return weighted_astar_path<World,
                             movement::LegacyWeighted<PassableTag, CostTag>>(
      world, request, scratch, policy);
}
