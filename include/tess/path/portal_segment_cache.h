#pragma once

#include <tess/core/tag_identity.h>
#include <tess/path/path.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace tess {

/// Result metadata for one movement-class-bound segment-cache lookup.
struct SegmentHit {
  bool found = false;
  PathStatus status = PathStatus::NoPath;
  std::uint32_t cost = 0;
};

/// Snapshot of weighted portal-segment cache occupancy and lifecycle counts.
struct PortalSegmentCacheStats {
  std::size_t entries = 0;
  std::size_t path_nodes = 0;
  std::size_t sweeps = 0;
  std::size_t evictions = 0;
  std::size_t stale_rejections = 0;
  // Whole-cache drops when for_class() binds a different movement class.
  std::size_t class_rebinds = 0;
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
// nothing. Entries belong to one movement class: `for_class()` safely clears
// the cache when that class changes. Keep one cache per (world, class) to avoid
// that conservative whole-cache fallback on the hot path.
/// Bounded, movement-class-safe cache of verified weighted path segments.
class WeightedPortalSegmentCache {
 public:
  static constexpr std::size_t default_segment_budget = 256;

  void set_segment_budget(std::size_t budget) noexcept {
    budget_ = budget;
    if (entries_.size() > budget_) {
      evict_oldest(entries_.size() - budget_);
    }
  }

  [[nodiscard]] auto segment_budget() const noexcept -> std::size_t {
    return budget_;
  }

  void reserve_segments(std::size_t count) { entries_.reserve(count); }

  void reserve_path_nodes(std::size_t count) { paths_.reserve(count); }

  void clear() noexcept {
    clear_storage();
    bound_class_ = 0;
  }

  void reset_stats() noexcept {
    sweeps_ = 0;
    evictions_ = 0;
    stale_rejections_ = 0;
    class_rebinds_ = 0;
  }

  [[nodiscard]] auto stats() const noexcept -> PortalSegmentCacheStats {
    return PortalSegmentCacheStats{
        entries_.size(), paths_.size(),     sweeps_,
        evictions_,      stale_rejections_, class_rebinds_,
    };
  }

  // A lightweight class-bound view. Each operation compares one precomputed
  // type token (no hashing or key growth), which also keeps an older view safe
  // if another class was bound in between. Rebinding clears entries because
  // their keys contain no movement-class data.
  template <typename Class>
  class ClassView {
   public:
    template <typename World>
    [[nodiscard]] auto lookup_append(const World& world, PathRequest request,
                                     std::vector<Coord3>& out_path)
        -> SegmentHit {
      cache_->bind_class(identity_);
      return cache_->lookup_append(world, request, out_path);
    }

    template <typename World>
    void store(const World& world, PathRequest request, PathResult result) {
      cache_->bind_class(identity_);
      cache_->store(world, request, result);
    }

   private:
    friend class WeightedPortalSegmentCache;
    ClassView(WeightedPortalSegmentCache& cache,
              std::uintptr_t identity) noexcept
        : cache_(&cache), identity_(identity) {}

    WeightedPortalSegmentCache* cache_;
    std::uintptr_t identity_;
  };

  template <typename ClassOrTag>
  [[nodiscard]] auto for_class() noexcept
      -> ClassView<movement::movement_class_of<ClassOrTag>> {
    using Class = movement::movement_class_of<ClassOrTag>;
    const auto identity = detail::tag_identity<Class>();
    bind_class(identity);
    return ClassView<Class>{*this, identity};
  }

  // One compaction pass keeping only entries whose chunk-version
  // dependencies still validate against `world`. Rebuilds the path-node
  // arena so storage held by dropped entries is reclaimed.
  template <typename World>
  void sweep_stale(const World& world) {
    compact(
        [&](const Entry& entry) { return entry.dependencies.is_valid(world); });
    ++sweeps_;
  }

  [[nodiscard]] auto size() const noexcept -> std::size_t {
    return entries_.size();
  }

 private:
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

    if (budget_ == 0 || result.status != PathStatus::Found) {
      return;
    }
    auto pending_stale_rejections = std::size_t{0};
    if (find(world, request, &pending_stale_rejections) != nullptr) {
      stale_rejections_ += pending_stale_rejections;
      return;
    }

    // Construct every potentially allocating per-entry dependency before
    // changing live cache storage. A failed capture therefore cannot publish a
    // route with only a prefix of its invalidation dependencies.
    auto entry = Entry{};
    entry.request = request;
    entry.status = result.status;
    entry.cost = result.cost;
    entry.path_size = result.path.size();
    for (const auto coord : result.path) {
      entry.dependencies.add_chunk(world,
                                   chunk_key<Shape>(tile_key<Shape>(coord)));
    }

    if (entries_.size() >= budget_) {
      // The transactional compaction reserves room for this entry as part of
      // its temporary representation. Once it commits, eviction and append are
      // allocation-free and cannot strand the cache between states.
      compact(
          [&](const Entry& current) {
            return current.dependencies.is_valid(world);
          },
          1, result.path.size());
      ++sweeps_;
      if (entries_.size() >= budget_) {
        evict_oldest(entries_.size() - budget_ + 1);
      }
    } else {
      reserve_append_capacity(1, result.path.size());
    }

    entry.path_offset = paths_.size();
    for (const auto coord : result.path) {
      paths_.push_back(coord);
    }
    entries_.push_back(std::move(entry));
    stale_rejections_ += pending_stale_rejections;
  }

  struct Entry {
    PathRequest request{};
    PathStatus status = PathStatus::NoPath;
    std::uint32_t cost = 0;
    std::size_t path_offset = 0;
    std::size_t path_size = 0;
    ChunkVersionDependencies dependencies{};
  };
  static_assert(std::is_nothrow_move_constructible_v<Entry>);
  static_assert(std::is_nothrow_move_assignable_v<Entry>);
  static_assert(std::is_nothrow_copy_constructible_v<Coord3>);

  template <typename World>
  [[nodiscard]] auto find(const World& world, PathRequest request,
                          std::size_t* pending_stale_rejections =
                              nullptr) noexcept -> const Entry* {
    for (const auto& entry : entries_) {
      if (entry.request.start != request.start ||
          entry.request.goal != request.goal) {
        continue;
      }
      if (!entry.dependencies.is_valid(world)) {
        if (pending_stale_rejections != nullptr) {
          ++*pending_stale_rejections;
        } else {
          ++stale_rejections_;
        }
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
  void evict_oldest(std::size_t count) noexcept {
    const auto evicted = count < entries_.size() ? count : entries_.size();
    if (evicted == 0) {
      return;
    }
    const auto first_path = evicted == entries_.size()
                                ? paths_.size()
                                : entries_[evicted].path_offset;
    std::move(paths_.begin() + static_cast<std::ptrdiff_t>(first_path),
              paths_.end(), paths_.begin());
    paths_.erase(paths_.end() - static_cast<std::ptrdiff_t>(first_path),
                 paths_.end());
    entries_.erase(entries_.begin(),
                   entries_.begin() + static_cast<std::ptrdiff_t>(evicted));
    for (auto& entry : entries_) {
      entry.path_offset -= first_path;
    }
    evictions_ += evicted;
  }

  void clear_storage() noexcept {
    entries_.clear();
    paths_.clear();
  }

  void bind_class(std::uintptr_t identity) noexcept {
    if (bound_class_ == identity) {
      return;
    }
    if (bound_class_ != 0) {
      clear_storage();
      ++class_rebinds_;
    }
    bound_class_ = identity;
  }

  void reserve_append_capacity(std::size_t additional_entries,
                               std::size_t additional_path_nodes) {
    if (additional_entries > entries_.max_size() - entries_.size() ||
        additional_path_nodes > paths_.max_size() - paths_.size()) {
      throw std::length_error{"portal segment cache capacity overflow"};
    }
    entries_.reserve(entries_.size() + additional_entries);
    paths_.reserve(paths_.size() + additional_path_nodes);
  }

  template <typename Keep>
  void compact(Keep keep, std::size_t additional_entries = 0,
               std::size_t additional_path_nodes = 0) {
    compact_entries_.clear();
    compact_paths_.clear();
    compact_indices_.clear();

    auto kept_path_nodes = std::size_t{0};
    for (std::size_t index = 0; index < entries_.size(); ++index) {
      const auto& entry = entries_[index];
      if (!keep(entry)) {
        continue;
      }
      if (entry.path_size > compact_paths_.max_size() - kept_path_nodes) {
        throw std::length_error{"portal segment path count exceeds max_size"};
      }
      kept_path_nodes += entry.path_size;
      compact_indices_.push_back(index);
    }

    if (additional_entries >
            compact_entries_.max_size() - compact_indices_.size() ||
        additional_path_nodes > compact_paths_.max_size() - kept_path_nodes) {
      throw std::length_error{"portal segment cache capacity overflow"};
    }
    compact_entries_.reserve(compact_indices_.size() + additional_entries);
    compact_paths_.reserve(kept_path_nodes + additional_path_nodes);

    // Everything below is non-throwing: capacities are fixed, Coord3 copies
    // and Entry moves are noexcept, and every source range was validated above.
    // Only now is it safe to move dependencies out of the live entries.
    for (const auto index : compact_indices_) {
      auto& entry = entries_[index];
      const auto offset = compact_paths_.size();
      for (std::size_t path_index = 0; path_index < entry.path_size;
           ++path_index) {
        compact_paths_.push_back(paths_[entry.path_offset + path_index]);
      }
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
  std::vector<std::size_t> compact_indices_;
  std::size_t budget_ = default_segment_budget;
  std::size_t sweeps_ = 0;
  std::size_t evictions_ = 0;
  std::size_t stale_rejections_ = 0;
  std::size_t class_rebinds_ = 0;
  std::uintptr_t bound_class_ = 0;
};

template <typename World, typename PassableTag, typename CostTag>
/// Builds a weighted portal route while reusing class-bound cached segments.
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

  std::vector<Coord3> stash;
  const auto source = product.stash_if_owned(waypoints, stash);

  product.clear();
  product.request_ = request;
  product.waypoints_.assign(source.begin(), source.end());
  auto class_cache =
      cache
          .template for_class<movement::LegacyWeighted<PassableTag, CostTag>>();

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
            class_cache.lookup_append(world, segment_request, product.path_);
        hit.found) {
      total_cost = detail::saturating_add(total_cost, hit.cost);
      return true;
    }

    const auto result = weighted_astar_path<World, PassableTag, CostTag>(
        world, segment_request, scratch);
    class_cache.store(world, segment_request, result);
    total_expanded += result.expanded_nodes;
    total_reached += result.reached_nodes;
    if (result.status != PathStatus::Found) {
      product.path_.clear();
      product.status_ = result.status;
      product.expanded_nodes_ = total_expanded;
      product.reached_nodes_ = total_reached;
      // Same failure-dependency contract as build_weighted_route_product;
      // the failing segment's endpoints are the offending tiles.
      detail::capture_failure_dependencies<Shape>(
          world, segment_request, result.status, product.dependencies_);
      return false;
    }
    total_cost = detail::saturating_add(total_cost, result.cost);
    append_path(result.path.span());
    return true;
  };

  for (const auto waypoint : source) {
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
