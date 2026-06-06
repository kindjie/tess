#pragma once

#include <tess/path/path.h>
#include <tess/path/portal_segment_cache.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace tess {

struct PathTicket {
  std::size_t value = 0;
};

struct PathRuntimeCachePolicy {
  std::size_t clear_every_world_change = 0;
  bool invalidate_unit_route_cache_on_world_change = true;
};

struct PathRuntimeStats {
  std::size_t submitted = 0;
  std::size_t completed = 0;
  std::size_t found = 0;
  std::size_t invalid_start = 0;
  std::size_t invalid_goal = 0;
  std::size_t no_path = 0;
  std::size_t world_cache_invalidations = 0;
  std::size_t cache_clears = 0;
  std::size_t path_nodes = 0;
  RouteCacheStats route_cache{};
  WeightedPathBatchStats weighted_batch{};
  std::size_t portal_segment_cache_entries = 0;
};

class PathRequestRuntime {
 public:
  void reserve_requests(std::size_t count) {
    requests_.reserve(count);
    results_.reserve(count);
    offsets_.reserve(count);
    sizes_.reserve(count);
    weighted_batch_.reserve_requests(count);
  }

  void reserve_path_nodes(std::size_t count) {
    paths_.reserve(count);
    unit_route_cache_.reserve_path_nodes(count);
    weighted_batch_.reserve_path_nodes(count);
    portal_segment_cache_.reserve_path_nodes(count);
  }

  void reserve_search_nodes(std::size_t count) {
    unit_scratch_.reserve_nodes(count);
    weighted_batch_.reserve_search_nodes(count);
  }

  void reserve_unit_routes(std::size_t count) {
    unit_route_cache_.reserve_routes(count);
  }

  void reserve_portal_segments(std::size_t count) {
    portal_segment_cache_.reserve_segments(count);
  }

  void clear_requests() noexcept {
    requests_.clear();
    clear_results();
  }

  void clear_caches() noexcept {
    unit_route_cache_.clear();
    portal_segment_cache_.clear();
    world_changes_since_clear_ = 0;
    ++cache_clears_;
  }

  [[nodiscard]] auto submit(PathRequest request) -> PathTicket {
    const auto ticket = PathTicket{requests_.size()};
    requests_.push_back(request);
    return ticket;
  }

  [[nodiscard]] auto requests() const noexcept -> std::span<const PathRequest> {
    return requests_;
  }

  [[nodiscard]] auto results() const noexcept -> std::span<const PathResult> {
    return results_;
  }

  [[nodiscard]] auto result(PathTicket ticket) const noexcept -> PathResult {
    return results_[ticket.value];
  }

  [[nodiscard]] auto route_cache() noexcept -> RouteCacheScratch& {
    return unit_route_cache_;
  }

  [[nodiscard]] auto route_cache() const noexcept -> const RouteCacheScratch& {
    return unit_route_cache_;
  }

  [[nodiscard]] auto portal_segment_cache() noexcept
      -> WeightedPortalSegmentCache& {
    return portal_segment_cache_;
  }

  [[nodiscard]] auto portal_segment_cache() const noexcept
      -> const WeightedPortalSegmentCache& {
    return portal_segment_cache_;
  }

  [[nodiscard]] auto stats() const noexcept -> PathRuntimeStats {
    auto stats = stats_;
    stats.submitted = requests_.size();
    stats.completed = results_.size();
    stats.path_nodes = paths_.size();
    stats.route_cache = unit_route_cache_.stats();
    stats.weighted_batch = weighted_batch_.stats();
    stats.portal_segment_cache_entries = portal_segment_cache_.size();
    stats.cache_clears = cache_clears_;
    return stats;
  }

  template <typename World, typename PassableTag>
  [[nodiscard]] auto process_unit_cached(const World& world,
                                         PathRuntimeCachePolicy policy = {})
      -> std::span<const PathResult> {
    clear_results();
    results_.resize(requests_.size());
    offsets_.assign(requests_.size(), 0);
    sizes_.assign(requests_.size(), 0);
    stats_ = {};
    prepare_process(world, policy);

    for (std::size_t i = 0; i < requests_.size(); ++i) {
      const auto result = cached_astar_path<World, PassableTag>(
          world, requests_[i], unit_scratch_, unit_route_cache_);
      copy_result(i, result);
      record_status(result.status);
    }
    refresh_result_spans();
    return results_;
  }

  template <typename World, typename PassableTag, typename CostTag,
            std::uint32_t MaxCost>
  [[nodiscard]] auto process_weighted_batch(const World& world,
                                            PathRuntimeCachePolicy policy = {})
      -> std::span<const PathResult> {
    clear_results();
    results_.resize(requests_.size());
    offsets_.assign(requests_.size(), 0);
    sizes_.assign(requests_.size(), 0);
    stats_ = {};
    prepare_process(world, policy);

    const auto batch =
        weighted_path_batch<World, PassableTag, CostTag, MaxCost>(
            world, requests_, weighted_batch_);
    for (std::size_t i = 0; i < batch.size(); ++i) {
      copy_result(i, batch[i]);
      record_status(batch[i].status);
    }
    refresh_result_spans();
    return results_;
  }

 private:
  void clear_results() noexcept {
    results_.clear();
    offsets_.clear();
    sizes_.clear();
    paths_.clear();
    stats_ = {};
  }

  template <typename World>
  void prepare_process(const World& world, PathRuntimeCachePolicy policy) {
    if (!policy.invalidate_unit_route_cache_on_world_change) {
      return;
    }
    if (!unit_route_cache_.invalidate_if_world_changed(world)) {
      return;
    }
    ++stats_.world_cache_invalidations;
    ++world_changes_since_clear_;
    if (policy.clear_every_world_change != 0 &&
        world_changes_since_clear_ >= policy.clear_every_world_change) {
      clear_caches();
    }
  }

  void copy_result(std::size_t index, PathResult result) {
    offsets_[index] = paths_.size();
    sizes_[index] = result.path.size();
    paths_.insert(paths_.end(), result.path.begin(), result.path.end());
    results_[index] = PathResult{
        result.status,        result.cost, result.expanded_nodes,
        result.reached_nodes, {},
    };
  }

  void refresh_result_spans() noexcept {
    for (std::size_t i = 0; i < results_.size(); ++i) {
      if (sizes_[i] == 0) {
        results_[i].path = {};
      } else {
        results_[i].path =
            std::span<const Coord3>{paths_.data() + offsets_[i], sizes_[i]};
      }
    }
  }

  void record_status(PathStatus status) noexcept {
    switch (status) {
      case PathStatus::Found:
        ++stats_.found;
        return;
      case PathStatus::InvalidStart:
        ++stats_.invalid_start;
        return;
      case PathStatus::InvalidGoal:
        ++stats_.invalid_goal;
        return;
      case PathStatus::NoPath:
        ++stats_.no_path;
        return;
    }
  }

  std::vector<PathRequest> requests_;
  std::vector<PathResult> results_;
  std::vector<std::size_t> offsets_;
  std::vector<std::size_t> sizes_;
  std::vector<Coord3> paths_;
  PathScratch unit_scratch_;
  RouteCacheScratch unit_route_cache_;
  WeightedPathBatchScratch weighted_batch_;
  WeightedPortalSegmentCache portal_segment_cache_;
  PathRuntimeStats stats_;
  std::size_t world_changes_since_clear_ = 0;
  std::size_t cache_clears_ = 0;
};

}  // namespace tess
