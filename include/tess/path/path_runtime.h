#pragma once

#include <tess/core/assert.h>
#include <tess/path/field_product_cache.h>
#include <tess/path/path.h>
#include <tess/path/portal_segment_cache.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace tess {

struct PathTicket {
  std::size_t value = 0;
};

struct PathRuntimeCachePolicy {
  std::size_t clear_every_world_change = 0;
  bool invalidate_unit_route_cache_on_world_change = true;
  bool use_unit_field_product_cache = false;
  std::size_t unit_field_product_min_goal_reuse = 2;
  std::size_t unit_field_product_min_start_chunks = 2;
  std::size_t unit_field_product_cache_byte_budget =
      std::numeric_limits<std::size_t>::max();
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
  FieldProductCacheStats field_product_cache{};
  std::size_t field_product_candidate_groups = 0;
  std::size_t field_product_used_groups = 0;
  std::size_t field_product_skipped_groups = 0;
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
    processed_.reserve(count);
    weighted_batch_.reserve_requests(count);
    unit_field_goals_.reserve(1);
  }

  void reserve_path_nodes(std::size_t count) {
    paths_.reserve(count);
    unit_route_cache_.reserve_path_nodes(count);
    unit_field_scratch_.reserve_nodes(count);
    unit_field_product_.reserve_nodes(count);
    weighted_batch_.reserve_path_nodes(count);
    portal_segment_cache_.reserve_path_nodes(count);
  }

  void reserve_search_nodes(std::size_t count) {
    unit_scratch_.reserve_nodes(count);
    unit_field_scratch_.reserve_nodes(count);
    unit_field_product_.reserve_nodes(count);
    weighted_batch_.reserve_search_nodes(count);
  }

  void reserve_unit_routes(std::size_t count) {
    unit_route_cache_.reserve_routes(count);
  }

  void reserve_unit_field_products(std::size_t count) {
    unit_field_product_cache_.reserve_entries(count);
  }

  void reserve_unit_field_product_dependencies(std::size_t count) {
    unit_field_product_.reserve_dependencies(count);
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
    unit_field_product_cache_.clear();
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
    TESS_ASSERT(ticket.value < results_.size());
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
    stats.field_product_cache = unit_field_product_cache_.stats();
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
    processed_.assign(requests_.size(), 0);
    stats_ = {};
    prepare_process(world, policy);
    if (policy.use_unit_field_product_cache) {
      process_repeated_goal_fields<World, PassableTag>(world, policy);
    }

    for (std::size_t i = 0; i < requests_.size(); ++i) {
      if (processed_[i] != 0) {
        continue;
      }
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
    processed_.assign(requests_.size(), 0);
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
    processed_.clear();
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

  template <typename World, typename PassableTag>
  void process_repeated_goal_fields(const World& world,
                                    PathRuntimeCachePolicy policy) {
    using Shape = typename World::shape_type;

    if (policy.unit_field_product_min_goal_reuse < 2) {
      policy.unit_field_product_min_goal_reuse = 2;
    }
    if (policy.unit_field_product_min_start_chunks == 0) {
      policy.unit_field_product_min_start_chunks = 1;
    }
    unit_field_product_cache_.set_byte_budget(
        policy.unit_field_product_cache_byte_budget);

    for (std::size_t i = 0; i < requests_.size(); ++i) {
      if (processed_[i] != 0) {
        continue;
      }
      auto grouped_by_previous_request = false;
      for (std::size_t j = 0; j < i; ++j) {
        if (requests_[j].goal == requests_[i].goal) {
          grouped_by_previous_request = true;
          break;
        }
      }
      if (grouped_by_previous_request) {
        continue;
      }

      auto reuse_count = std::size_t{0};
      auto start_chunk_count = std::size_t{0};
      for (std::size_t j = i; j < requests_.size(); ++j) {
        if (processed_[j] == 0 && requests_[j].goal == requests_[i].goal) {
          ++reuse_count;
          const auto chunk =
              chunk_key<Shape>(tile_key<Shape>(requests_[j].start));
          auto seen = false;
          for (std::size_t k = i; k < j; ++k) {
            if (processed_[k] != 0 || requests_[k].goal != requests_[i].goal) {
              continue;
            }
            if (chunk_key<Shape>(tile_key<Shape>(requests_[k].start)) ==
                chunk) {
              seen = true;
              break;
            }
          }
          if (!seen) {
            ++start_chunk_count;
          }
        }
      }
      if (reuse_count < policy.unit_field_product_min_goal_reuse) {
        continue;
      }
      ++stats_.field_product_candidate_groups;
      if (start_chunk_count < policy.unit_field_product_min_start_chunks) {
        ++stats_.field_product_skipped_groups;
        continue;
      }

      unit_field_goals_.clear();
      unit_field_goals_.add(requests_[i].goal);
      auto* product =
          unit_field_product_cache_.template lookup<World, PassableTag>(
              world, unit_field_goals_);
      if (product == nullptr) {
        const auto field = build_distance_field_product<World, PassableTag>(
            world, unit_field_goals_, unit_field_scratch_, unit_field_product_);
        if (field.status == PathStatus::Found) {
          (void)unit_field_product_cache_.template store<World, PassableTag>(
              unit_field_product_);
          product =
              unit_field_product_cache_.template lookup<World, PassableTag>(
                  world, unit_field_goals_);
        }
      }

      if (product == nullptr) {
        ++stats_.field_product_skipped_groups;
        continue;
      }

      ++stats_.field_product_used_groups;
      for (std::size_t j = i; j < requests_.size(); ++j) {
        if (processed_[j] != 0 || requests_[j].goal != requests_[i].goal) {
          continue;
        }
        const auto result = distance_field_product_path<World, PassableTag>(
            world, requests_[j].start, *product, unit_field_scratch_);
        copy_result(j, result);
        record_status(result.status);
        processed_[j] = 1;
      }
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
  std::vector<std::uint8_t> processed_;
  std::vector<Coord3> paths_;
  PathScratch unit_scratch_;
  RouteCacheScratch unit_route_cache_;
  DistanceFieldScratch unit_field_scratch_;
  GoalSet unit_field_goals_;
  DistanceFieldProduct unit_field_product_;
  FieldProductCache unit_field_product_cache_;
  WeightedPathBatchScratch weighted_batch_;
  WeightedPortalSegmentCache portal_segment_cache_;
  PathRuntimeStats stats_;
  std::size_t world_changes_since_clear_ = 0;
  std::size_t cache_clears_ = 0;
};

}  // namespace tess
