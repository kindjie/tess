#pragma once

#include <tess/path/path.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace tess {

struct SegmentHit {
  bool found = false;
  PathStatus status = PathStatus::NoPath;
  std::uint32_t cost = 0;
};

struct PortalSegmentCacheStats {
  std::size_t entries = 0;
  std::size_t path_nodes = 0;
  std::size_t sweeps = 0;
  std::size_t evictions = 0;
  std::size_t stale_rejections = 0;
};

// Cached segment paths are only handed out by appending into caller-owned
// storage. The cache never returns pointers or spans into its own path
// storage, so later `store()` growth cannot invalidate a previous lookup.
//
// Storage is bounded by a segment budget (default 256 entries). When a store
// reaches the budget it first sweeps stale entries in one compaction pass
// (rebuilding both the entry list and the path-node append arena, so stale
// path storage is reclaimed), then evicts the oldest live entries in
// insertion order if the sweep alone cannot make room. A zero budget stores
// nothing.
class WeightedPortalSegmentCache {
 public:
  static constexpr std::size_t default_segment_budget = 256;

  void set_segment_budget(std::size_t budget) noexcept { budget_ = budget; }

  [[nodiscard]] auto segment_budget() const noexcept -> std::size_t {
    return budget_;
  }

  void reserve_segments(std::size_t count) { entries_.reserve(count); }

  void reserve_path_nodes(std::size_t count) { paths_.reserve(count); }

  void clear() noexcept {
    entries_.clear();
    paths_.clear();
  }

  void reset_stats() noexcept {
    sweeps_ = 0;
    evictions_ = 0;
    stale_rejections_ = 0;
  }

  [[nodiscard]] auto stats() const noexcept -> PortalSegmentCacheStats {
    return PortalSegmentCacheStats{
        entries_.size(), paths_.size(), sweeps_, evictions_, stale_rejections_,
    };
  }

  // Appends the cached path for `request` into `out_path` on a hit. When
  // `out_path` already ends with the segment start (stitching consecutive
  // segments), the shared junction node is appended only once. Misses and
  // stale entries leave `out_path` untouched.
  template <typename World>
  [[nodiscard]] auto lookup_append(const World& world, PathRequest request,
                                   std::vector<Coord3>& out_path)
      -> SegmentHit {
    const auto* entry = find(world, request);
    if (entry == nullptr) {
      return SegmentHit{};
    }
    const auto cached = path(*entry);
    const auto stitch = !out_path.empty() && !cached.empty() &&
                        out_path.back() == cached.front();
    out_path.insert(out_path.end(),
                    cached.begin() + (stitch ? std::ptrdiff_t{1} : 0),
                    cached.end());
    return SegmentHit{true, entry->status, entry->cost};
  }

  template <typename World>
  void store(const World& world, PathRequest request, PathResult result) {
    using Shape = World::shape_type;

    if (budget_ == 0 || result.status != PathStatus::Found ||
        find(world, request) != nullptr) {
      return;
    }
    if (entries_.size() >= budget_) {
      sweep_stale(world);
      if (entries_.size() >= budget_) {
        evict_oldest(entries_.size() - budget_ + 1);
      }
    }
    const auto offset = paths_.size();
    paths_.insert(paths_.end(), result.path.begin(), result.path.end());
    auto& entry = entries_.emplace_back();
    entry.request = request;
    entry.status = result.status;
    entry.cost = result.cost;
    entry.path_offset = offset;
    entry.path_size = result.path.size();
    for (const auto coord : result.path) {
      entry.dependencies.add_chunk(world,
                                   chunk_key<Shape>(tile_key<Shape>(coord)));
    }
  }

  // One compaction pass keeping only entries whose chunk-version
  // dependencies still validate against `world`. Rebuilds the path-node
  // arena so storage held by dropped entries is reclaimed.
  template <typename World>
  void sweep_stale(const World& world) {
    ++sweeps_;
    compact(
        [&](const Entry& entry) { return entry.dependencies.is_valid(world); });
  }

  [[nodiscard]] auto size() const noexcept -> std::size_t {
    return entries_.size();
  }

 private:
  struct Entry {
    PathRequest request{};
    PathStatus status = PathStatus::NoPath;
    std::uint32_t cost = 0;
    std::size_t path_offset = 0;
    std::size_t path_size = 0;
    ChunkVersionDependencies dependencies{};
  };

  template <typename World>
  [[nodiscard]] auto find(const World& world, PathRequest request) noexcept
      -> const Entry* {
    for (const auto& entry : entries_) {
      if (entry.request.start != request.start ||
          entry.request.goal != request.goal) {
        continue;
      }
      if (!entry.dependencies.is_valid(world)) {
        ++stale_rejections_;
        continue;
      }
      return &entry;
    }
    return nullptr;
  }

  [[nodiscard]] auto path(const Entry& entry) const noexcept
      -> std::span<const Coord3> {
    return std::span<const Coord3>{paths_.data() + entry.path_offset,
                                   entry.path_size};
  }

  // Drops the `count` oldest entries (insertion order) via compaction so
  // their path-node storage is reclaimed with them.
  void evict_oldest(std::size_t count) {
    const auto evicted = count < entries_.size() ? count : entries_.size();
    auto index = std::size_t{0};
    compact([&](const Entry&) { return index++ >= count; });
    evictions_ += evicted;
  }

  template <typename Keep>
  void compact(Keep keep) {
    compact_entries_.clear();
    compact_paths_.clear();
    for (auto& entry : entries_) {
      if (!keep(entry)) {
        continue;
      }
      const auto offset = compact_paths_.size();
      compact_paths_.insert(
          compact_paths_.end(),
          paths_.begin() + static_cast<std::ptrdiff_t>(entry.path_offset),
          paths_.begin() +
              static_cast<std::ptrdiff_t>(entry.path_offset + entry.path_size));
      entry.path_offset = offset;
      compact_entries_.push_back(std::move(entry));
    }
    entries_.swap(compact_entries_);
    paths_.swap(compact_paths_);
  }

  std::vector<Entry> entries_;
  std::vector<Coord3> paths_;
  std::vector<Entry> compact_entries_;
  std::vector<Coord3> compact_paths_;
  std::size_t budget_ = default_segment_budget;
  std::size_t sweeps_ = 0;
  std::size_t evictions_ = 0;
  std::size_t stale_rejections_ = 0;
};

template <typename World, typename PassableTag, typename CostTag>
auto build_weighted_portal_route_product(const World& world,
                                         PathRequest request,
                                         std::span<const Coord3> waypoints,
                                         PathScratch& scratch,
                                         WeightedPortalSegmentCache& cache,
                                         WeightedPortalRouteProduct& product)
    -> PathResult {
  using Shape = World::shape_type;
  // Caches weighted portal segments keyed by chunk topology and tracks
  // chunk-version dependencies; dense-only until sparse topology (Slice 4) and
  // the sparse route-cache slice. Its per-segment weighted A* already runs
  // natively on sparse worlds via weighted_astar_path.
  static_assert(
      std::is_same_v<typename World::residency_type, AlwaysResident>,
      "build_weighted_portal_route_product (segment cache) is dense-only; it "
      "needs sparse topology and route-cache support from a later slice.");

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
    if (const auto hit =
            cache.lookup_append(world, segment_request, product.path_);
        hit.found) {
      total_cost = detail::saturating_add(total_cost, hit.cost);
      return true;
    }

    const auto result = weighted_astar_path<World, PassableTag, CostTag>(
        world, segment_request, scratch);
    cache.store(world, segment_request, result);
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
    append_path(result.path.span());
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
