#pragma once

#include <tess/path/path.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace tess {

namespace detail {

template <typename World, typename PassableTag>
[[nodiscard]] auto build_greedy_chunk_portal_candidate(
    const World& world, PathRequest request, std::vector<Coord3>& waypoints)
    -> PortalRouteCandidate {
  using Shape = typename World::shape_type;

  waypoints.clear();
  auto current = request.start;
  auto current_chunk = chunk_coord<Shape>(request.start);
  const auto goal_chunk = chunk_coord<Shape>(request.goal);
  auto result = PortalRouteCandidate{true, 0, 0};

  while (current_chunk != goal_chunk) {
    auto found_step = false;
    auto best_score = std::numeric_limits<std::uint32_t>::max();
    auto best_chunk = ChunkCoord3{};
    auto best_portal = Coord3{};

    const auto consider = [&](ChunkCoord3 next_chunk) {
      auto portal = Coord3{};
      auto scan_tiles = std::size_t{0};
      if (!best_chunk_portal<World, PassableTag>(
              world, current_chunk, next_chunk, current, request.goal, portal,
              &scan_tiles)) {
        result.scan_tiles += scan_tiles;
        return;
      }
      result.scan_tiles += scan_tiles;
      const auto score = saturating_add(manhattan(current, portal),
                                        manhattan(portal, request.goal));
      if (!found_step || score < best_score) {
        found_step = true;
        best_score = score;
        best_chunk = next_chunk;
        best_portal = portal;
      }
    };

    if (current_chunk.x != goal_chunk.x) {
      auto next = current_chunk;
      if (current_chunk.x < goal_chunk.x) {
        ++next.x;
      } else {
        --next.x;
      }
      consider(next);
    }
    if (current_chunk.y != goal_chunk.y) {
      auto next = current_chunk;
      if (current_chunk.y < goal_chunk.y) {
        ++next.y;
      } else {
        --next.y;
      }
      consider(next);
    }
    if (current_chunk.z != goal_chunk.z) {
      auto next = current_chunk;
      if (current_chunk.z < goal_chunk.z) {
        ++next.z;
      } else {
        --next.z;
      }
      consider(next);
    }

    if (!found_step) {
      result.found = false;
      return result;
    }
    result.score =
        saturating_add(result.score, manhattan(current, best_portal));
    waypoints.push_back(best_portal);
    current = best_portal;
    current_chunk = best_chunk;
  }

  result.score = saturating_add(result.score, manhattan(current, request.goal));
  return result;
}

}  // namespace detail

template <typename World, typename PassableTag, typename CostTag>
auto build_weighted_chunk_portal_route_product(
    const World& world, PathRequest request, PathScratch& scratch,
    WeightedPortalRouteProduct& product) -> PathResult {
  using Shape = typename World::shape_type;

  product.clear();
  product.request_ = request;

  if (!contains<Shape>(request.start)) {
    product.status_ = PathStatus::InvalidStart;
    return PathResult{product.status_, 0, 0, 0, product.path_};
  }
  if (!contains<Shape>(request.goal)) {
    product.status_ = PathStatus::InvalidGoal;
    return PathResult{product.status_, 0, 0, 0, product.path_};
  }

  constexpr auto orders = std::array{
      std::array{detail::Axis::X, detail::Axis::Y, detail::Axis::Z},
      std::array{detail::Axis::X, detail::Axis::Z, detail::Axis::Y},
      std::array{detail::Axis::Y, detail::Axis::X, detail::Axis::Z},
      std::array{detail::Axis::Y, detail::Axis::Z, detail::Axis::X},
      std::array{detail::Axis::Z, detail::Axis::X, detail::Axis::Y},
      std::array{detail::Axis::Z, detail::Axis::Y, detail::Axis::X},
  };

  auto found_route = false;
  auto best_score = std::numeric_limits<std::uint32_t>::max();
  product.best_waypoints_.clear();
  for (const auto& order : orders) {
    const auto candidate =
        detail::build_chunk_portal_candidate<World, PassableTag>(
            world, request, order, product.candidate_waypoints_);
    ++product.route_candidates_;
    product.portal_scan_tiles_ += candidate.scan_tiles;
    if (!candidate.found) {
      continue;
    }
    if (!found_route || candidate.score < best_score) {
      found_route = true;
      best_score = candidate.score;
      product.best_waypoints_.assign(product.candidate_waypoints_.begin(),
                                     product.candidate_waypoints_.end());
    }
  }
  {
    const auto candidate =
        detail::build_greedy_chunk_portal_candidate<World, PassableTag>(
            world, request, product.candidate_waypoints_);
    ++product.route_candidates_;
    product.portal_scan_tiles_ += candidate.scan_tiles;
    if (candidate.found && (!found_route || candidate.score < best_score)) {
      found_route = true;
      best_score = candidate.score;
      product.best_waypoints_.assign(product.candidate_waypoints_.begin(),
                                     product.candidate_waypoints_.end());
    }
  }
  if (!found_route) {
    product.status_ = PathStatus::NoPath;
    return PathResult{product.status_, 0, 0, 0, product.path_};
  }
  product.waypoints_.assign(product.best_waypoints_.begin(),
                            product.best_waypoints_.end());

  auto from = request.start;
  auto total_cost = std::uint32_t{0};
  auto total_expanded = std::size_t{0};
  auto total_reached = std::size_t{0};
  auto append_segment = [&](PathRequest segment_request) {
    const auto result = weighted_astar_path<World, PassableTag, CostTag>(
        world, segment_request, scratch);
    total_expanded += result.expanded_nodes;
    total_reached += result.reached_nodes;
    if (result.status != PathStatus::Found) {
      product.path_.clear();
      product.status_ = result.status;
      product.expanded_nodes_ = total_expanded;
      product.reached_nodes_ = total_reached;
      return false;
    }
    total_cost = detail::saturating_add(total_cost, result.cost);
    product.segment_.assign(result.path.begin(), result.path.end());
    for (std::size_t i = product.path_.empty() ? 0u : 1u;
         i < product.segment_.size(); ++i) {
      product.path_.push_back(product.segment_[i]);
    }
    return true;
  };

  for (const auto waypoint : product.waypoints_) {
    if (!append_segment(PathRequest{from, waypoint})) {
      return PathResult{product.status_, 0, total_expanded, total_reached,
                        product.path_};
    }
    from = waypoint;
  }
  if (!append_segment(PathRequest{from, request.goal})) {
    return PathResult{product.status_, 0, total_expanded, total_reached,
                      product.path_};
  }

  product.status_ = PathStatus::Found;
  product.cost_ = total_cost;
  product.expanded_nodes_ = total_expanded;
  product.reached_nodes_ = total_reached;
  for (const auto coord : product.path_) {
    const auto key = tile_key<Shape>(coord);
    product.dependencies_.add_chunk(world, chunk_key<Shape>(key));
  }
  return PathResult{product.status_, product.cost_, product.expanded_nodes_,
                    product.reached_nodes_, product.path_};
}

}  // namespace tess
