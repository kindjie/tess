#pragma once

#include <tess/core/assert.h>
#include <tess/path/field_product_cache.h>
#include <tess/path/path.h>
#include <tess/path/portal_segment_cache.h>
#include <tess/path/precheck.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>
#include <vector>

namespace tess {

struct PathTicket {
  std::size_t value = 0;
  std::uint32_t generation = 0;
};

struct PathRuntimeCachePolicy {
  std::size_t clear_every_world_change = 0;
  bool invalidate_unit_route_cache_on_world_change = true;
  bool use_unit_field_product_cache = false;
  std::size_t unit_field_product_min_goal_reuse = 2;
  std::size_t unit_field_product_min_start_chunks = 2;
  std::size_t unit_field_product_cache_byte_budget =
      std::numeric_limits<std::size_t>::max();
  std::size_t max_route_entries = RouteCacheScratch::default_max_entries;
  std::size_t max_route_path_nodes = RouteCacheScratch::default_max_path_nodes;
  std::size_t portal_segment_budget =
      WeightedPortalSegmentCache::default_segment_budget;
};

struct PathRuntimeStats {
  std::size_t submitted = 0;
  std::size_t completed = 0;
  std::size_t found = 0;
  std::size_t invalid_start = 0;
  std::size_t invalid_goal = 0;
  std::size_t no_path = 0;
  // Sparse worlds: the search could not rule out a route through a
  // non-resident chunk (PathStatus::Indeterminate). Kept distinct from
  // no_path so a stale/partial residency set is never counted as "no route".
  std::size_t indeterminate = 0;
  // Requests an optional topology precheck proved unreachable before A*, so no
  // grid was expanded for them. A SUBSET of no_path (each ruled-out request is
  // also counted there): the result is the same NoPath A* would have returned,
  // this counter only measures how many were resolved without searching.
  std::size_t precheck_ruled_out = 0;
  std::size_t world_cache_invalidations = 0;
  std::size_t cache_clears = 0;
  std::size_t path_nodes = 0;
  RouteCacheStats route_cache{};
  FieldProductCacheStats field_product_cache{};
  std::size_t field_product_candidate_groups = 0;
  std::size_t field_product_used_groups = 0;
  std::size_t field_product_skipped_groups = 0;
  WeightedPathBatchStats weighted_batch{};
  PortalSegmentCacheStats portal_segment_cache{};
};

class PathRequestRuntime {
 public:
  void reserve_requests(std::size_t count) {
    requests_.reserve(count);
    results_.reserve(count);
    offsets_.reserve(count);
    sizes_.reserve(count);
    processed_.reserve(count);
    request_group_.reserve(count);
    group_members_.reserve(count);
    group_start_chunks_.reserve(count);
    precheck_survivors_.reserve(count);
    survivor_original_.reserve(count);
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
    ++generation_;
  }

  void clear_caches() noexcept {
    unit_route_cache_.clear();
    unit_field_product_cache_.clear();
    portal_segment_cache_.clear();
    world_changes_since_clear_ = 0;
    ++cache_clears_;
  }

  [[nodiscard]] auto submit(PathRequest request) -> PathTicket {
    const auto ticket = PathTicket{requests_.size(), generation_};
    requests_.push_back(request);
    return ticket;
  }

  [[nodiscard]] auto requests() const noexcept -> std::span<const PathRequest> {
    return requests_;
  }

  [[nodiscard]] auto results() const noexcept -> std::span<const PathResult> {
    return results_;
  }

  // Tickets are stamped with the submission generation; clear_requests()
  // starts a new generation, so a ticket held across it is stale even when
  // it aliases an in-range slot of the new batch. Stale or out-of-range
  // tickets assert in debug builds and report NoPath in release builds.
  [[nodiscard]] auto result(PathTicket ticket) const noexcept -> PathResult {
    TESS_ASSERT(ticket.generation == generation_);
    TESS_ASSERT(ticket.value < results_.size());
    if (ticket.generation != generation_ || ticket.value >= results_.size()) {
      auto stale = PathResult{};
      stale.status = PathStatus::NoPath;
      return stale;
    }
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
    stats.portal_segment_cache = portal_segment_cache_.stats();
    stats.cache_clears = cache_clears_;
    return stats;
  }

  template <typename World, typename PassableTag>
  [[nodiscard]] auto process_unit_cached(
      const World& world, PathRuntimeCachePolicy policy = {},
      const RegionGraphT<typename World::residency_type>* graph = nullptr)
      -> std::span<const PathResult> {
    clear_results();
    results_.resize(requests_.size());
    offsets_.assign(requests_.size(), 0);
    sizes_.assign(requests_.size(), 0);
    processed_.assign(requests_.size(), 0);
    stats_ = {};
    prepare_process(world, policy);
    if (graph != nullptr) {
      precheck_prepass(world, *graph);
    }
    if constexpr (std::is_same_v<typename World::residency_type,
                                 AlwaysResident>) {
      // The unit field-product cache is dense-only (it sizes distance arrays by
      // the global tile count). if constexpr, not a runtime if, so the
      // dense-only process_repeated_goal_fields is never instantiated for a
      // sparse world; the sparse unit path routes each request through
      // cached_astar_path until a sparse field-product slice lands.
      if (policy.use_unit_field_product_cache) {
        process_repeated_goal_fields<World, PassableTag>(world, policy);
      }
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
  [[nodiscard]] auto process_weighted_batch(
      const World& world, PathRuntimeCachePolicy policy = {},
      const RegionGraphT<typename World::residency_type>* graph = nullptr)
      -> std::span<const PathResult> {
    clear_results();
    results_.resize(requests_.size());
    offsets_.assign(requests_.size(), 0);
    sizes_.assign(requests_.size(), 0);
    processed_.assign(requests_.size(), 0);
    stats_ = {};
    prepare_process(world, policy);

    if (graph == nullptr) {
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

    // Precheck partitions the batch: requests proven unreachable resolve to
    // NoPath now, and only the survivors run through weighted A*. The batch is
    // positional, so survivor results scatter back to their original slots.
    precheck_prepass(world, *graph);
    precheck_survivors_.clear();
    survivor_original_.clear();
    for (std::size_t i = 0; i < requests_.size(); ++i) {
      if (processed_[i] == 0) {
        survivor_original_.push_back(i);
        precheck_survivors_.push_back(requests_[i]);
      }
    }
    const auto batch =
        weighted_path_batch<World, PassableTag, CostTag, MaxCost>(
            world, precheck_survivors_, weighted_batch_);
    for (std::size_t s = 0; s < batch.size(); ++s) {
      const auto i = survivor_original_[s];
      copy_result(i, batch[s]);
      record_status(batch[s].status);
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
    unit_route_cache_.set_caps(policy.max_route_entries,
                               policy.max_route_path_nodes);
    portal_segment_cache_.set_segment_budget(policy.portal_segment_budget);
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

  // Groups repeated-goal requests in one O(n) pass: a reusable
  // open-addressed flat map (power-of-two capacity, linear probing) assigns
  // each distinct goal a group id in first-occurrence order, member indices
  // are bucketed with a counting sort, and per-group distinct start chunks
  // come from a sort+unique over reusable scratch. Group processing order
  // and all stats semantics match the previous per-request rescan.
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

    constexpr auto no_group = std::numeric_limits<std::uint32_t>::max();
    group_goals_.clear();
    group_counts_.clear();
    request_group_.assign(requests_.size(), no_group);
    auto slot_capacity = goal_group_slots_.size() < 16u
                             ? std::size_t{16}
                             : goal_group_slots_.size();
    while (slot_capacity < (requests_.size() + 1u) * 2u) {
      slot_capacity *= 2u;
    }
    goal_group_slots_.assign(slot_capacity, 0u);
    const auto slot_mask = slot_capacity - 1u;
    for (std::size_t i = 0; i < requests_.size(); ++i) {
      if (processed_[i] != 0) {
        continue;
      }
      const auto goal = requests_[i].goal;
      auto slot =
          static_cast<std::size_t>(detail::coord_hash(goal)) & slot_mask;
      auto group = no_group;
      while (goal_group_slots_[slot] != 0u) {
        const auto candidate = goal_group_slots_[slot] - 1u;
        if (group_goals_[candidate] == goal) {
          group = candidate;
          break;
        }
        slot = (slot + 1u) & slot_mask;
      }
      if (group == no_group) {
        group = static_cast<std::uint32_t>(group_goals_.size());
        goal_group_slots_[slot] = group + 1u;
        group_goals_.push_back(goal);
        group_counts_.push_back(0u);
      }
      request_group_[i] = group;
      ++group_counts_[group];
    }

    // Bucket member indices per group; members stay in submission order.
    group_offsets_.assign(group_goals_.size() + 1u, 0u);
    for (std::size_t i = 0; i < requests_.size(); ++i) {
      if (request_group_[i] != no_group) {
        ++group_offsets_[request_group_[i] + 1u];
      }
    }
    for (std::size_t g = 1; g < group_offsets_.size(); ++g) {
      group_offsets_[g] += group_offsets_[g - 1u];
    }
    group_cursors_.assign(group_offsets_.begin(), group_offsets_.end());
    group_members_.assign(group_offsets_.back(), 0u);
    for (std::size_t i = 0; i < requests_.size(); ++i) {
      if (request_group_[i] != no_group) {
        group_members_[group_cursors_[request_group_[i]]++] =
            static_cast<std::uint32_t>(i);
      }
    }

    for (std::uint32_t g = 0; g < group_goals_.size(); ++g) {
      if (group_counts_[g] < policy.unit_field_product_min_goal_reuse) {
        continue;
      }
      const auto members_begin = group_offsets_[g];
      const auto members_end = group_offsets_[g + 1u];
      group_start_chunks_.clear();
      for (auto m = members_begin; m < members_end; ++m) {
        const auto& request = requests_[group_members_[m]];
        group_start_chunks_.push_back(
            chunk_key<Shape>(tile_key<Shape>(request.start)).value);
      }
      std::sort(group_start_chunks_.begin(), group_start_chunks_.end());
      const auto start_chunk_count = static_cast<std::size_t>(
          std::unique(group_start_chunks_.begin(), group_start_chunks_.end()) -
          group_start_chunks_.begin());

      ++stats_.field_product_candidate_groups;
      if (start_chunk_count < policy.unit_field_product_min_start_chunks) {
        ++stats_.field_product_skipped_groups;
        continue;
      }

      unit_field_goals_.clear();
      unit_field_goals_.add(group_goals_[g]);
      auto* product =
          unit_field_product_cache_.template lookup<World, PassableTag>(
              world, unit_field_goals_);
      if (product == nullptr) {
        const auto field = build_distance_field_product<World, PassableTag>(
            world, unit_field_goals_, unit_field_scratch_, unit_field_product_);
        if (field.status == PathStatus::Found) {
          // The cache takes the product by move; the next rebuild through
          // build_distance_field_product() clears and reassigns
          // unit_field_product_, so the moved-from state is never observed.
          (void)unit_field_product_cache_.template store<World, PassableTag>(
              std::move(unit_field_product_));
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
      for (auto m = members_begin; m < members_end; ++m) {
        const auto j = static_cast<std::size_t>(group_members_[m]);
        const auto result = distance_field_product_path<World, PassableTag>(
            world, requests_[j].start, *product, unit_field_scratch_);
        copy_result(j, result);
        record_status(result.status);
        processed_[j] = 1;
      }
    }
  }

  // Resolves every not-yet-processed request the region graph proves
  // unreachable to a NoPath result without running A*, marking it processed so
  // downstream field-product grouping and the search loop skip it. The graph's
  // freshness and no-false-negative guarantees live in precheck_path.
  template <typename World>
  void precheck_prepass(
      const World& world,
      const RegionGraphT<typename World::residency_type>& graph) {
    for (std::size_t i = 0; i < requests_.size(); ++i) {
      if (processed_[i] != 0) {
        continue;
      }
      const auto status = precheck_path(graph, world, requests_[i].start,
                                        requests_[i].goal, precheck_scratch_);
      if (!precheck_rules_out_path(status)) {
        continue;
      }
      auto ruled_out = PathResult{};
      ruled_out.status = PathStatus::NoPath;
      copy_result(i, ruled_out);
      record_status(ruled_out.status);
      ++stats_.precheck_ruled_out;
      processed_[i] = 1;
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
      case PathStatus::Indeterminate:
        ++stats_.indeterminate;
        return;
    }
  }

  std::vector<PathRequest> requests_;
  std::vector<PathResult> results_;
  std::vector<std::size_t> offsets_;
  std::vector<std::size_t> sizes_;
  std::vector<std::uint8_t> processed_;
  std::vector<Coord3> paths_;
  // Optional pre-A* topology precheck: reused BFS scratch plus the survivor
  // partition (surviving requests and their original slot indices) that lets
  // the monolithic weighted batch skip proven-unreachable requests.
  RegionGraphScratch precheck_scratch_;
  std::vector<PathRequest> precheck_survivors_;
  std::vector<std::size_t> survivor_original_;
  // Reusable repeated-goal grouping scratch: flat-hash goal map slots,
  // per-group goals/counts/member buckets, and start-chunk dedup storage.
  std::vector<std::uint32_t> goal_group_slots_;
  std::vector<Coord3> group_goals_;
  std::vector<std::uint32_t> group_counts_;
  std::vector<std::uint32_t> group_offsets_;
  std::vector<std::uint32_t> group_cursors_;
  std::vector<std::uint32_t> group_members_;
  std::vector<std::uint32_t> request_group_;
  std::vector<std::uint64_t> group_start_chunks_;
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
  std::uint32_t generation_ = 0;
};

}  // namespace tess
