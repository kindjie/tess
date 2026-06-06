#pragma once

#include <tess/path/path.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace tess {

class WeightedPortalSegmentCache {
 public:
  struct Entry {
    PathRequest request{};
    PathStatus status = PathStatus::NoPath;
    std::uint32_t cost = 0;
    std::size_t path_offset = 0;
    std::size_t path_size = 0;
  };

  void reserve_segments(std::size_t count) { entries_.reserve(count); }

  void reserve_path_nodes(std::size_t count) { paths_.reserve(count); }

  void clear() noexcept {
    entries_.clear();
    paths_.clear();
  }

  [[nodiscard]] auto find(PathRequest request) const noexcept -> const Entry* {
    for (const auto& entry : entries_) {
      if (entry.request.start == request.start &&
          entry.request.goal == request.goal) {
        return &entry;
      }
    }
    return nullptr;
  }

  [[nodiscard]] auto path(const Entry& entry) const noexcept
      -> std::span<const Coord3> {
    return std::span<const Coord3>{paths_.data() + entry.path_offset,
                                   entry.path_size};
  }

  void store(PathRequest request, PathResult result) {
    if (find(request) != nullptr) {
      return;
    }
    const auto offset = paths_.size();
    paths_.insert(paths_.end(), result.path.begin(), result.path.end());
    entries_.push_back(
        Entry{request, result.status, result.cost, offset, result.path.size()});
  }

  [[nodiscard]] auto size() const noexcept -> std::size_t {
    return entries_.size();
  }

 private:
  std::vector<Entry> entries_;
  std::vector<Coord3> paths_;
};

template <typename World, typename PassableTag, typename CostTag>
auto build_weighted_portal_route_product(const World& world,
                                         PathRequest request,
                                         std::span<const Coord3> waypoints,
                                         PathScratch& scratch,
                                         WeightedPortalSegmentCache& cache,
                                         WeightedPortalRouteProduct& product)
    -> PathResult {
  using Shape = typename World::shape_type;

  product.clear();
  product.request_ = request;
  product.waypoints_.assign(waypoints.begin(), waypoints.end());

  auto from = request.start;
  auto total_cost = std::uint32_t{0};
  auto total_expanded = std::size_t{0};
  auto total_reached = std::size_t{0};
  auto append_path = [&](std::span<const Coord3> path) {
    for (std::size_t i = product.path_.empty() ? 0u : 1u; i < path.size();
         ++i) {
      product.path_.push_back(path[i]);
    }
  };
  auto append_segment = [&](PathRequest segment_request) {
    if (const auto* entry = cache.find(segment_request); entry != nullptr) {
      if (entry->status != PathStatus::Found) {
        product.path_.clear();
        product.status_ = entry->status;
        product.expanded_nodes_ = total_expanded;
        product.reached_nodes_ = total_reached;
        return false;
      }
      total_cost = detail::saturating_add(total_cost, entry->cost);
      append_path(cache.path(*entry));
      return true;
    }

    const auto result = weighted_astar_path<World, PassableTag, CostTag>(
        world, segment_request, scratch);
    cache.store(segment_request, result);
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
    append_path(result.path);
    return true;
  };

  for (const auto waypoint : waypoints) {
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
