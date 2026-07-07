#pragma once

#define TESS_PATH_PATH_H_INCLUDED 1

#include <tess/core/shape.h>
#include <tess/diagnostics/diagnostics.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace tess {

enum class PathStatus : std::uint8_t {
  Found,
  InvalidStart,
  InvalidGoal,
  NoPath,
};
static_assert(sizeof(PathStatus) == sizeof(std::uint8_t));

struct PathRequest {
  Coord3 start;
  Coord3 goal;
};

struct PathResult {
  PathStatus status = PathStatus::NoPath;
  std::uint32_t cost = 0;
  std::size_t expanded_nodes = 0;
  std::size_t reached_nodes = 0;
  std::span<const Coord3> path;
};

struct DistanceFieldResult {
  PathStatus status = PathStatus::NoPath;
  std::size_t expanded_nodes = 0;
  std::size_t reached_nodes = 0;
};

struct WeightedPathBatchStats {
  std::size_t requests = 0;
  std::size_t unique_goals = 0;
  std::size_t field_builds = 0;
  std::size_t astar_fallbacks = 0;
  std::size_t path_nodes = 0;
};

class PathScratch;
class DistanceFieldScratch;
class GoalSet;
class DistanceFieldProduct;
class RouteCacheScratch;
struct NearestTargetResult;

template <typename World, typename Tag>
auto build_distance_field_product(const World& world, const GoalSet& goals,
                                  DistanceFieldScratch& scratch,
                                  DistanceFieldProduct& product)
    -> DistanceFieldResult;

template <typename World, typename Tag>
auto distance_field_product_path(const World& world, Coord3 start,
                                 const DistanceFieldProduct& product,
                                 DistanceFieldScratch& scratch) -> PathResult;

template <typename World, typename Tag>
auto nearest_target(const World& world, Coord3 start,
                    const DistanceFieldProduct& product,
                    DistanceFieldScratch& scratch) -> NearestTargetResult;

class ChunkVersionDependencies {
 public:
  struct ChunkVersionDependency {
    ChunkKey key{};
    std::uint32_t version = 0;
  };

  void reserve(std::size_t count) { chunks_.reserve(count); }

  void clear() noexcept { chunks_.clear(); }

  template <typename World>
  void capture_all(const World& world) {
    chunks_.clear();
    chunks_.reserve(static_cast<std::size_t>(World::chunk_count));
    for (std::uint64_t i = 0; i < World::chunk_count; ++i) {
      add_chunk(world, ChunkKey{i});
    }
  }

  template <typename World>
  void add_chunk(const World& world, ChunkKey key) {
    const auto version = world.meta(key).version;
    // Path nodes are chunk-coherent: consecutive additions usually repeat
    // the previous chunk, so check the last entry before scanning.
    if (!chunks_.empty() && chunks_.back().key == key) {
      chunks_.back().version = version;
      return;
    }
    for (auto& chunk : chunks_) {
      if (chunk.key == key) {
        chunk.version = version;
        return;
      }
    }
    chunks_.push_back(ChunkVersionDependency{key, version});
  }

  template <typename World>
  [[nodiscard]] auto is_valid(const World& world) const noexcept -> bool {
    for (const auto chunk : chunks_) {
      if (world.meta(chunk.key).version != chunk.version) {
        return false;
      }
    }
    return true;
  }

  [[nodiscard]] auto size() const noexcept -> std::size_t {
    return chunks_.size();
  }

  [[nodiscard]] auto chunks() const noexcept
      -> std::span<const ChunkVersionDependency> {
    return chunks_;
  }

 private:
  std::vector<ChunkVersionDependency> chunks_;
};

class WeightedRouteProduct {
 public:
  void reserve_path_nodes(std::size_t node_count) { path_.reserve(node_count); }

  void reserve_dependencies(std::size_t count) { dependencies_.reserve(count); }

  void clear() noexcept {
    request_ = {};
    status_ = PathStatus::NoPath;
    cost_ = 0;
    expanded_nodes_ = 0;
    reached_nodes_ = 0;
    path_.clear();
    dependencies_.clear();
  }

  template <typename World>
  [[nodiscard]] auto is_valid(const World& world) const noexcept -> bool {
    return dependencies_.is_valid(world);
  }

  [[nodiscard]] auto request() const noexcept -> PathRequest {
    return request_;
  }

  [[nodiscard]] auto dependencies() const noexcept
      -> std::span<const ChunkVersionDependencies::ChunkVersionDependency> {
    return dependencies_.chunks();
  }

 private:
  template <typename World, typename PassableTag, typename CostTag>
  friend auto build_weighted_route_product(const World& world,
                                           PathRequest request,
                                           PathScratch& scratch,
                                           WeightedRouteProduct& product)
      -> PathResult;

  template <typename World>
  friend auto weighted_route_product_path(const World& world,
                                          const WeightedRouteProduct& product)
      -> PathResult;

  PathRequest request_{};
  PathStatus status_ = PathStatus::NoPath;
  std::uint32_t cost_ = 0;
  std::size_t expanded_nodes_ = 0;
  std::size_t reached_nodes_ = 0;
  std::vector<Coord3> path_;
  ChunkVersionDependencies dependencies_;
};

class WeightedPortalSegmentCache;

class WeightedPortalRouteProduct {
 public:
  void reserve_waypoints(std::size_t count) {
    waypoints_.reserve(count);
    candidate_waypoints_.reserve(count);
    best_waypoints_.reserve(count);
  }

  void reserve_path_nodes(std::size_t node_count) {
    path_.reserve(node_count);
    segment_.reserve(node_count);
  }

  void reserve_dependencies(std::size_t count) { dependencies_.reserve(count); }

  void clear() noexcept {
    request_ = {};
    status_ = PathStatus::NoPath;
    cost_ = 0;
    expanded_nodes_ = 0;
    reached_nodes_ = 0;
    route_candidates_ = 0;
    portal_scan_tiles_ = 0;
    waypoints_.clear();
    candidate_waypoints_.clear();
    best_waypoints_.clear();
    path_.clear();
    segment_.clear();
    dependencies_.clear();
  }

  template <typename World>
  [[nodiscard]] auto is_valid(const World& world) const noexcept -> bool {
    return dependencies_.is_valid(world);
  }

  [[nodiscard]] auto request() const noexcept -> PathRequest {
    return request_;
  }

  [[nodiscard]] auto waypoints() const noexcept -> std::span<const Coord3> {
    return waypoints_;
  }

  [[nodiscard]] auto route_candidates() const noexcept -> std::size_t {
    return route_candidates_;
  }

  [[nodiscard]] auto portal_scan_tiles() const noexcept -> std::size_t {
    return portal_scan_tiles_;
  }

 private:
  template <typename World, typename PassableTag, typename CostTag>
  friend auto build_weighted_portal_route_product(
      const World& world, PathRequest request,
      std::span<const Coord3> waypoints, PathScratch& scratch,
      WeightedPortalRouteProduct& product) -> PathResult;

  template <typename World, typename PassableTag, typename CostTag>
  friend auto build_weighted_portal_route_product(
      const World& world, PathRequest request,
      std::span<const Coord3> waypoints, PathScratch& scratch,
      WeightedPortalSegmentCache& cache, WeightedPortalRouteProduct& product)
      -> PathResult;

  template <typename World, typename PassableTag, typename CostTag>
  friend auto build_weighted_chunk_portal_route_product(
      const World& world, PathRequest request, PathScratch& scratch,
      WeightedPortalRouteProduct& product) -> PathResult;

  template <typename World>
  friend auto weighted_portal_route_product_path(
      const World& world, const WeightedPortalRouteProduct& product)
      -> PathResult;

  PathRequest request_{};
  PathStatus status_ = PathStatus::NoPath;
  std::uint32_t cost_ = 0;
  std::size_t expanded_nodes_ = 0;
  std::size_t reached_nodes_ = 0;
  std::size_t route_candidates_ = 0;
  std::size_t portal_scan_tiles_ = 0;
  std::vector<Coord3> waypoints_;
  std::vector<Coord3> candidate_waypoints_;
  std::vector<Coord3> best_waypoints_;
  std::vector<Coord3> path_;
  std::vector<Coord3> segment_;
  ChunkVersionDependencies dependencies_;
};

class PathScratch {
 public:
  struct OpenNode {
    std::uint64_t index = 0;
    std::uint32_t g = 0;
    std::uint32_t f = 0;
  };

  void reserve_nodes(std::size_t node_count) {
    open_.reserve(node_count);
    open_next_.reserve(node_count);
    generation_.reserve(node_count);
    state_.reserve(node_count);
    g_.reserve(node_count);
    parent_.reserve(node_count);
    touched_.reserve(node_count);
    path_.reserve(node_count);
  }

  void clear() noexcept {
    advance_epoch();
    open_.clear();
    open_next_.clear();
    touched_.clear();
    path_.clear();
  }

  [[nodiscard]] auto capacity_nodes() const noexcept -> std::size_t {
    return state_.capacity();
  }

 private:
  template <typename World, typename Tag>
  friend auto astar_path(const World& world, PathRequest request,
                         PathScratch& scratch) -> PathResult;

  template <typename World, typename PassableTag, typename CostTag>
  friend auto weighted_astar_path(const World& world, PathRequest request,
                                  PathScratch& scratch) -> PathResult;

  template <typename World, typename Tag>
  friend auto cached_astar_path(const World& world, PathRequest request,
                                PathScratch& scratch, RouteCacheScratch& cache)
      -> PathResult;

  void advance_epoch() noexcept {
    ++epoch_;
    if (epoch_ == 0) {
      std::fill(generation_.begin(), generation_.end(), 0);
      epoch_ = 1;
    }
  }

  [[nodiscard]] auto is_current(std::size_t offset) const noexcept -> bool {
    return generation_[offset] == epoch_;
  }

  [[nodiscard]] auto state_at(std::size_t offset,
                              std::uint8_t unseen) const noexcept
      -> std::uint8_t {
    return is_current(offset) ? state_[offset] : unseen;
  }

  [[nodiscard]] auto g_at(std::size_t offset,
                          std::uint32_t infinite_cost) const noexcept
      -> std::uint32_t {
    return is_current(offset) ? g_[offset] : infinite_cost;
  }

  void touch_node(std::uint64_t index) {
    const auto offset = static_cast<std::size_t>(index);
    generation_[offset] = epoch_;
    touched_.push_back(index);
  }

  std::vector<OpenNode> open_;
  std::vector<OpenNode> open_next_;
  std::vector<std::uint32_t> generation_;
  std::uint32_t epoch_ = 1;
  std::vector<std::uint8_t> state_;
  std::vector<std::uint32_t> g_;
  std::vector<std::uint64_t> parent_;
  std::vector<std::uint64_t> touched_;
  std::vector<Coord3> path_;
};

class DistanceFieldScratch {
 public:
  void reserve_nodes(std::size_t node_count) {
    frontier_.reserve(node_count);
    weighted_frontier_.reserve(node_count);
    weighted_bucket_capacity_ = node_count / 8u + 1u;
    for (auto& bucket : weighted_buckets_) {
      bucket.reserve(weighted_bucket_capacity_);
    }
    generation_.reserve(node_count);
    distance_.reserve(node_count);
    touched_.reserve(node_count);
    path_.reserve(node_count);
  }

  [[nodiscard]] auto capacity_nodes() const noexcept -> std::size_t {
    return distance_.capacity();
  }

 private:
  template <typename World, typename Tag>
  friend auto build_distance_field(const World& world, Coord3 goal,
                                   DistanceFieldScratch& scratch)
      -> DistanceFieldResult;

  template <typename World, typename Tag>
  friend auto distance_field_path(const World& world, Coord3 start, Coord3 goal,
                                  DistanceFieldScratch& scratch) -> PathResult;

  template <typename World, typename PassableTag, typename CostTag>
  friend auto build_weighted_distance_field(const World& world, Coord3 goal,
                                            DistanceFieldScratch& scratch)
      -> DistanceFieldResult;

  template <typename World, typename PassableTag, typename CostTag>
  friend auto build_weighted_distance_field_in_box(
      const World& world, Coord3 goal, Box3 domain,
      DistanceFieldScratch& scratch) -> DistanceFieldResult;

  template <typename World, typename PassableTag, typename CostTag,
            std::uint32_t MaxCost>
  friend auto build_bounded_weighted_distance_field(
      const World& world, Coord3 goal, DistanceFieldScratch& scratch)
      -> DistanceFieldResult;

  template <typename World, typename PassableTag, typename CostTag>
  friend auto weighted_distance_field_path(const World& world, Coord3 start,
                                           Coord3 goal,
                                           DistanceFieldScratch& scratch)
      -> PathResult;

  template <typename World, typename Tag>
  friend auto build_distance_field_product(const World& world,
                                           const GoalSet& goals,
                                           DistanceFieldScratch& scratch,
                                           DistanceFieldProduct& product)
      -> DistanceFieldResult;

  template <typename World, typename Tag>
  friend auto distance_field_product_path(const World& world, Coord3 start,
                                          const DistanceFieldProduct& product,
                                          DistanceFieldScratch& scratch)
      -> PathResult;

  template <typename World, typename Tag>
  friend auto nearest_target(const World& world, Coord3 start,
                             const DistanceFieldProduct& product,
                             DistanceFieldScratch& scratch)
      -> NearestTargetResult;

  void clear_build() noexcept {
    advance_epoch();
    frontier_.clear();
    weighted_frontier_.clear();
    for (auto& bucket : weighted_buckets_) {
      bucket.clear();
    }
    touched_.clear();
    path_.clear();
    has_goal_ = false;
  }

  void clear_path() noexcept { path_.clear(); }

  void advance_epoch() noexcept {
    ++epoch_;
    if (epoch_ == 0) {
      std::fill(generation_.begin(), generation_.end(), 0);
      epoch_ = 1;
    }
  }

  [[nodiscard]] auto is_current(std::size_t offset) const noexcept -> bool {
    return generation_[offset] == epoch_;
  }

  [[nodiscard]] auto distance_at(std::size_t offset,
                                 std::uint32_t infinite_distance) const noexcept
      -> std::uint32_t {
    return is_current(offset) ? distance_[offset] : infinite_distance;
  }

  void touch_node(std::uint64_t index) {
    const auto offset = static_cast<std::size_t>(index);
    generation_[offset] = epoch_;
    touched_.push_back(index);
  }

  std::vector<std::uint64_t> frontier_;
  std::vector<PathScratch::OpenNode> weighted_frontier_;
  std::vector<std::vector<std::uint64_t>> weighted_buckets_;
  std::size_t weighted_bucket_capacity_ = 0;
  std::vector<std::uint32_t> generation_;
  std::uint32_t epoch_ = 1;
  std::vector<std::uint32_t> distance_;
  std::vector<std::uint64_t> touched_;
  std::vector<Coord3> path_;
  Coord3 goal_{};
  bool has_goal_ = false;
};

class WeightedPathBatchScratch {
 public:
  void reserve_requests(std::size_t request_count) {
    results_.reserve(request_count);
    offsets_.reserve(request_count);
    sizes_.reserve(request_count);
    processed_.reserve(request_count);
  }

  void reserve_path_nodes(std::size_t node_count) {
    paths_.reserve(node_count);
  }

  void reserve_search_nodes(std::size_t node_count) {
    field_scratch_.reserve_nodes(node_count);
    astar_scratch_.reserve_nodes(node_count);
  }

  void clear() noexcept {
    results_.clear();
    offsets_.clear();
    sizes_.clear();
    processed_.clear();
    paths_.clear();
    stats_ = {};
  }

  [[nodiscard]] auto stats() const noexcept -> WeightedPathBatchStats {
    return stats_;
  }

 private:
  template <typename World, typename PassableTag, typename CostTag,
            std::uint32_t MaxCost>
  friend auto weighted_path_batch(const World& world,
                                  std::span<const PathRequest> requests,
                                  WeightedPathBatchScratch& scratch)
      -> std::span<const PathResult>;

  DistanceFieldScratch field_scratch_;
  PathScratch astar_scratch_;
  std::vector<PathResult> results_;
  std::vector<std::size_t> offsets_;
  std::vector<std::size_t> sizes_;
  std::vector<std::uint8_t> processed_;
  std::vector<Coord3> paths_;
  WeightedPathBatchStats stats_;
};

namespace detail {

enum class Axis : std::uint8_t {
  X,
  Y,
  Z,
};

struct PortalRouteCandidate {
  bool found = false;
  std::uint32_t score = 0;
  std::size_t scan_tiles = 0;
};

[[nodiscard]] constexpr auto abs_delta(std::int64_t lhs,
                                       std::int64_t rhs) noexcept
    -> std::uint64_t {
  return lhs < rhs ? static_cast<std::uint64_t>(rhs - lhs)
                   : static_cast<std::uint64_t>(lhs - rhs);
}

[[nodiscard]] constexpr auto manhattan(Coord3 lhs, Coord3 rhs) noexcept
    -> std::uint32_t {
  const auto distance = abs_delta(lhs.x, rhs.x) + abs_delta(lhs.y, rhs.y) +
                        abs_delta(lhs.z, rhs.z);
  if (distance > std::numeric_limits<std::uint32_t>::max()) {
    return std::numeric_limits<std::uint32_t>::max();
  }
  return static_cast<std::uint32_t>(distance);
}

template <typename World>
[[nodiscard]] constexpr auto tile_count() noexcept -> std::size_t {
  static_assert(World::chunk_count <= std::numeric_limits<std::size_t>::max() /
                                          World::local_tile_count);
  return static_cast<std::size_t>(World::chunk_count * World::local_tile_count);
}

template <typename Shape>
[[nodiscard]] constexpr auto tile_index(Coord3 coord) noexcept
    -> std::uint64_t {
  static_assert(ShapeTraits<Shape>::tile_key_bits <= 64,
                "MVP pathfinding supports shapes with u64 tile keys.");
  return static_cast<std::uint64_t>(tile_key<Shape>(coord).value);
}

template <typename Shape>
[[nodiscard]] constexpr auto tile_coord(std::uint64_t index) noexcept
    -> Coord3 {
  using Storage = typename ShapeTraits<Shape>::TileKeyStorage;
  return coord<Shape>(TileKey<Shape>{static_cast<Storage>(index)});
}

template <typename World, typename Tag>
[[nodiscard]] auto is_passable(const World& world, Coord3 coord) noexcept
    -> bool {
  const auto* value = world.template try_field<Tag>(coord);
  return value != nullptr && static_cast<bool>(*value);
}

template <typename World, typename Tag>
[[nodiscard]] auto is_passable_index(const World& world,
                                     std::uint64_t index) noexcept -> bool {
  using Shape = typename World::shape_type;
  using Storage = typename ShapeTraits<Shape>::TileKeyStorage;
  const auto key = TileKey<Shape>{static_cast<Storage>(index)};
  const auto& value = world.chunk(chunk_key<Shape>(key))
                          .template field<Tag>(local_tile_id<Shape>(key));
  return static_cast<bool>(value);
}

template <typename World, typename Tag>
[[nodiscard]] auto tile_entry_cost_index(const World& world,
                                         std::uint64_t index) noexcept
    -> std::uint32_t {
  using Shape = typename World::shape_type;
  using Storage = typename ShapeTraits<Shape>::TileKeyStorage;
  const auto key = TileKey<Shape>{static_cast<Storage>(index)};
  TESS_DIAG_EVENT(path_cost_read);
  const auto& value = world.chunk(chunk_key<Shape>(key))
                          .template field<Tag>(local_tile_id<Shape>(key));
  static_assert(std::is_integral_v<std::remove_cvref_t<decltype(value)>>,
                "weighted_astar_path requires an integral cost field.");
  if constexpr (std::is_signed_v<std::remove_cvref_t<decltype(value)>>) {
    if (value <= 0) {
      return 0;
    }
  } else if (value == 0) {
    return 0;
  }
  if (static_cast<std::uint64_t>(value) >
      std::numeric_limits<std::uint32_t>::max()) {
    return std::numeric_limits<std::uint32_t>::max();
  }
  return static_cast<std::uint32_t>(value);
}

[[nodiscard]] constexpr auto saturating_add(std::uint32_t lhs,
                                            std::uint32_t rhs) noexcept
    -> std::uint32_t {
  if (rhs > std::numeric_limits<std::uint32_t>::max() - lhs) {
    return std::numeric_limits<std::uint32_t>::max();
  }
  return lhs + rhs;
}

template <typename World, typename Tag>
[[nodiscard]] auto is_full_axis_barrier(const World& world, Coord3 blocked,
                                        Axis axis) noexcept -> bool {
  using Shape = typename World::shape_type;
  constexpr auto size = ShapeTraits<Shape>::size;

  if (axis == Axis::X) {
    for (std::int64_t z = 0; z < static_cast<std::int64_t>(size.z); ++z) {
      for (std::int64_t y = 0; y < static_cast<std::int64_t>(size.y); ++y) {
        const auto coord = Coord3{blocked.x, y, z};
        TESS_DIAG_EVENT(path_passability_check);
        if (is_passable_index<World, Tag>(world, tile_index<Shape>(coord))) {
          return false;
        }
      }
    }
    return true;
  }

  if (axis == Axis::Y) {
    for (std::int64_t z = 0; z < static_cast<std::int64_t>(size.z); ++z) {
      for (std::int64_t x = 0; x < static_cast<std::int64_t>(size.x); ++x) {
        const auto coord = Coord3{x, blocked.y, z};
        TESS_DIAG_EVENT(path_passability_check);
        if (is_passable_index<World, Tag>(world, tile_index<Shape>(coord))) {
          return false;
        }
      }
    }
    return true;
  }

  for (std::int64_t y = 0; y < static_cast<std::int64_t>(size.y); ++y) {
    for (std::int64_t x = 0; x < static_cast<std::int64_t>(size.x); ++x) {
      const auto coord = Coord3{x, y, blocked.z};
      TESS_DIAG_EVENT(path_passability_check);
      if (is_passable_index<World, Tag>(world, tile_index<Shape>(coord))) {
        return false;
      }
    }
  }
  return true;
}

template <typename Shape>
[[nodiscard]] constexpr auto chunk_origin(ChunkCoord3 chunk) noexcept
    -> Coord3 {
  constexpr auto size = ShapeTraits<Shape>::chunk;
  return Coord3{
      static_cast<std::int64_t>(chunk.x * size.x),
      static_cast<std::int64_t>(chunk.y * size.y),
      static_cast<std::int64_t>(chunk.z * size.z),
  };
}

template <typename Shape>
[[nodiscard]] constexpr auto adjacent_chunk(ChunkCoord3 from,
                                            ChunkCoord3 to) noexcept -> bool {
  const auto dx = from.x > to.x ? from.x - to.x : to.x - from.x;
  const auto dy = from.y > to.y ? from.y - to.y : to.y - from.y;
  const auto dz = from.z > to.z ? from.z - to.z : to.z - from.z;
  return dx + dy + dz == 1;
}

template <typename World, typename PassableTag>
[[nodiscard]] auto best_chunk_portal(const World& world, ChunkCoord3 from,
                                     ChunkCoord3 to, Coord3 current,
                                     Coord3 goal, Coord3& portal,
                                     std::size_t* scan_tiles = nullptr) noexcept
    -> bool {
  using Shape = typename World::shape_type;
  constexpr auto chunk = ShapeTraits<Shape>::chunk;
  const auto origin = chunk_origin<Shape>(from);

  if (!adjacent_chunk<Shape>(from, to)) {
    return false;
  }

  auto found = false;
  auto best_score = std::numeric_limits<std::uint32_t>::max();
  const auto consider = [&](Coord3 source, Coord3 target) {
    if (scan_tiles != nullptr) {
      ++(*scan_tiles);
    }
    TESS_DIAG_EVENT(path_passability_check);
    if (!is_passable<World, PassableTag>(world, source)) {
      return;
    }
    TESS_DIAG_EVENT(path_passability_check);
    if (!is_passable<World, PassableTag>(world, target)) {
      return;
    }
    const auto score =
        saturating_add(manhattan(current, target), manhattan(target, goal));
    if (!found || score < best_score) {
      found = true;
      best_score = score;
      portal = target;
    }
  };

  if (from.x != to.x) {
    const auto step = from.x < to.x ? std::int64_t{1} : std::int64_t{-1};
    const auto source_x =
        step > 0 ? origin.x + static_cast<std::int64_t>(chunk.x) - 1 : origin.x;
    for (std::int64_t z = origin.z;
         z < origin.z + static_cast<std::int64_t>(chunk.z); ++z) {
      for (std::int64_t y = origin.y;
           y < origin.y + static_cast<std::int64_t>(chunk.y); ++y) {
        consider(Coord3{source_x, y, z}, Coord3{source_x + step, y, z});
      }
    }
    return found;
  }

  if (from.y != to.y) {
    const auto step = from.y < to.y ? std::int64_t{1} : std::int64_t{-1};
    const auto source_y =
        step > 0 ? origin.y + static_cast<std::int64_t>(chunk.y) - 1 : origin.y;
    for (std::int64_t z = origin.z;
         z < origin.z + static_cast<std::int64_t>(chunk.z); ++z) {
      for (std::int64_t x = origin.x;
           x < origin.x + static_cast<std::int64_t>(chunk.x); ++x) {
        consider(Coord3{x, source_y, z}, Coord3{x, source_y + step, z});
      }
    }
    return found;
  }

  const auto step = from.z < to.z ? std::int64_t{1} : std::int64_t{-1};
  const auto source_z =
      step > 0 ? origin.z + static_cast<std::int64_t>(chunk.z) - 1 : origin.z;
  for (std::int64_t y = origin.y;
       y < origin.y + static_cast<std::int64_t>(chunk.y); ++y) {
    for (std::int64_t x = origin.x;
         x < origin.x + static_cast<std::int64_t>(chunk.x); ++x) {
      consider(Coord3{x, y, source_z}, Coord3{x, y, source_z + step});
    }
  }
  return found;
}

template <typename World, typename PassableTag>
[[nodiscard]] auto build_chunk_portal_candidate(const World& world,
                                                PathRequest request,
                                                std::span<const Axis> order,
                                                std::vector<Coord3>& waypoints)
    -> PortalRouteCandidate {
  using Shape = typename World::shape_type;

  waypoints.clear();
  auto current = request.start;
  auto current_chunk = chunk_coord<Shape>(request.start);
  const auto goal_chunk = chunk_coord<Shape>(request.goal);
  auto result = PortalRouteCandidate{true, 0, 0};

  const auto append_portal = [&](ChunkCoord3 next_chunk) {
    auto portal = Coord3{};
    if (!best_chunk_portal<World, PassableTag>(world, current_chunk, next_chunk,
                                               current, request.goal, portal,
                                               &result.scan_tiles)) {
      result.found = false;
      return false;
    }
    result.score = saturating_add(result.score, manhattan(current, portal));
    waypoints.push_back(portal);
    current = portal;
    current_chunk = next_chunk;
    return true;
  };

  for (const auto axis : order) {
    if (axis == Axis::X) {
      while (current_chunk.x != goal_chunk.x) {
        auto next = current_chunk;
        if (current_chunk.x < goal_chunk.x) {
          ++next.x;
        } else {
          --next.x;
        }
        if (!append_portal(next)) {
          return result;
        }
      }
    } else if (axis == Axis::Y) {
      while (current_chunk.y != goal_chunk.y) {
        auto next = current_chunk;
        if (current_chunk.y < goal_chunk.y) {
          ++next.y;
        } else {
          --next.y;
        }
        if (!append_portal(next)) {
          return result;
        }
      }
    } else {
      while (current_chunk.z != goal_chunk.z) {
        auto next = current_chunk;
        if (current_chunk.z < goal_chunk.z) {
          ++next.z;
        } else {
          --next.z;
        }
        if (!append_portal(next)) {
          return result;
        }
      }
    }
  }

  result.score = saturating_add(result.score, manhattan(current, request.goal));
  return result;
}

template <typename Fn>
void for_each_axis_neighbor(Coord3 coord, Fn&& fn) {
  fn(Coord3{coord.x + 1, coord.y, coord.z});
  fn(Coord3{coord.x - 1, coord.y, coord.z});
  fn(Coord3{coord.x, coord.y + 1, coord.z});
  fn(Coord3{coord.x, coord.y - 1, coord.z});
  fn(Coord3{coord.x, coord.y, coord.z + 1});
  fn(Coord3{coord.x, coord.y, coord.z - 1});
}

template <typename Shape, typename Fn>
void for_each_indexed_axis_neighbor(Coord3 coord, std::uint64_t index,
                                    Fn&& fn) {
  using Traits = ShapeTraits<Shape>;
  constexpr auto size = Traits::size;
  constexpr auto chunk = Traits::chunk;
  constexpr auto local_bits = Traits::local_bits;
  constexpr auto chunk_index_stride =
      local_bits >= 64 ? std::uint64_t{0} : (std::uint64_t{1} << local_bits);
  constexpr auto chunk_y_stride = Traits::chunk_count_x * chunk_index_stride;

  const auto local_x = static_cast<std::uint64_t>(coord.x) & (chunk.x - 1);
  const auto local_y = static_cast<std::uint64_t>(coord.y) & (chunk.y - 1);

  if constexpr (!Traits::degenerate_x) {
    if (static_cast<std::uint64_t>(coord.x) + 1 < size.x) {
      const auto next_index = local_x + 1 < chunk.x
                                  ? index + 1
                                  : index + chunk_index_stride - local_x;
      fn(Coord3{coord.x + 1, coord.y, coord.z}, next_index);
    }
    if (coord.x > 0) {
      const auto next_index =
          local_x > 0 ? index - 1 : index - chunk_index_stride + (chunk.x - 1);
      fn(Coord3{coord.x - 1, coord.y, coord.z}, next_index);
    }
  }

  if constexpr (!Traits::degenerate_y) {
    if (static_cast<std::uint64_t>(coord.y) + 1 < size.y) {
      const auto next_index = local_y + 1 < chunk.y
                                  ? index + chunk.x
                                  : index + chunk_y_stride - local_y * chunk.x;
      fn(Coord3{coord.x, coord.y + 1, coord.z}, next_index);
    }
    if (coord.y > 0) {
      const auto next_index =
          local_y > 0 ? index - chunk.x
                      : index - chunk_y_stride + (chunk.y - 1) * chunk.x;
      fn(Coord3{coord.x, coord.y - 1, coord.z}, next_index);
    }
  }

  if constexpr (!Traits::degenerate_z) {
    constexpr auto chunk_z_stride =
        Traits::chunk_count_x * Traits::chunk_count_y * chunk_index_stride;
    const auto local_xy = chunk.x * chunk.y;
    const auto local_z = static_cast<std::uint64_t>(coord.z) & (chunk.z - 1);
    if (static_cast<std::uint64_t>(coord.z) + 1 < size.z) {
      const auto next_index = local_z + 1 < chunk.z
                                  ? index + local_xy
                                  : index + chunk_z_stride - local_z * local_xy;
      fn(Coord3{coord.x, coord.y, coord.z + 1}, next_index);
    }
    if (coord.z > 0) {
      const auto next_index =
          local_z > 0 ? index - local_xy
                      : index - chunk_z_stride + (chunk.z - 1) * local_xy;
      fn(Coord3{coord.x, coord.y, coord.z - 1}, next_index);
    }
  }
}

[[nodiscard]] constexpr bool open_node_less(
    PathScratch::OpenNode lhs, PathScratch::OpenNode rhs) noexcept {
  if (lhs.f != rhs.f) {
    return lhs.f > rhs.f;
  }
  if (lhs.g != rhs.g) {
    return lhs.g < rhs.g;
  }
  return lhs.index > rhs.index;
}

}  // namespace detail

template <typename World, typename Tag>
auto astar_path(const World& world, PathRequest request, PathScratch& scratch)
    -> PathResult {
  using Shape = typename World::shape_type;
  constexpr auto unseen = std::uint8_t{0};
  constexpr auto open = std::uint8_t{1};
  constexpr auto closed = std::uint8_t{2};
  constexpr auto no_parent = std::numeric_limits<std::uint64_t>::max();
  constexpr auto infinite_cost = std::numeric_limits<std::uint32_t>::max();

  TESS_DIAG_EVENT_VALUE(path_clear, scratch.touched_.size());
  scratch.clear();
  if (!contains<Shape>(request.start)) {
    return PathResult{PathStatus::InvalidStart, 0, 0, 0, scratch.path_};
  }
  TESS_DIAG_EVENT(path_start_passability_check);
  if (!detail::is_passable<World, Tag>(world, request.start)) {
    return PathResult{PathStatus::InvalidStart, 0, 0, 0, scratch.path_};
  }
  if (!contains<Shape>(request.goal)) {
    return PathResult{PathStatus::InvalidGoal, 0, 0, 0, scratch.path_};
  }
  TESS_DIAG_EVENT(path_goal_passability_check);
  if (!detail::is_passable<World, Tag>(world, request.goal)) {
    return PathResult{PathStatus::InvalidGoal, 0, 0, 0, scratch.path_};
  }

  auto direct_current = request.start;
  auto direct_blocked_by_barrier = false;
  auto direct_blocked_coord = request.start;
  auto direct_blocked_axis = detail::Axis::X;
  const auto step_direct_axis = [&](auto Coord3::* member, detail::Axis axis) {
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
  const auto try_direct_order = [&](auto first_member, detail::Axis first_axis,
                                    auto second_member,
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

  const auto append_segment_2d = [&](Coord3 from, Coord3 to, auto first_member,
                                     auto second_member) {
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
    if constexpr (!(ShapeTraits<Shape>::degenerate_x ||
                    ShapeTraits<Shape>::degenerate_y ||
                    ShapeTraits<Shape>::degenerate_z)) {
      return false;
    }
    if (is_degenerate_axis(direct_blocked_axis)) {
      return false;
    }

    const auto scan_axis = other_2d_axis(direct_blocked_axis);
    const auto blocked_member = coord_member(direct_blocked_axis);
    const auto scan_member = coord_member(scan_axis);

    auto best_gap = Coord3{0, 0, 0};
    auto best_cost = infinite_cost;
    for (std::int64_t value = 0;
         value < static_cast<std::int64_t>(axis_extent(scan_axis)); ++value) {
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
    const auto first_leg =
        append_segment_2d(request.start, best_gap, blocked_member,
                          scan_member) ||
        append_segment_2d(request.start, best_gap, scan_member, blocked_member);
    if (!first_leg) {
      scratch.path_.clear();
      return false;
    }
    const auto second_leg =
        append_segment_2d(best_gap, request.goal, blocked_member,
                          scan_member) ||
        append_segment_2d(best_gap, request.goal, scan_member, blocked_member);
    if (!second_leg) {
      scratch.path_.clear();
      return false;
    }
    plane_gap_cost = best_cost;
    return true;
  };
  if (try_plane_gap_route_2d()) {
    return PathResult{PathStatus::Found, plane_gap_cost, scratch.path_.size(),
                      scratch.path_.size(), scratch.path_};
  }

  const auto append_segment_3d = [&](Coord3 from, Coord3 to, auto first_member,
                                     auto second_member, auto third_member) {
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
    if constexpr (ShapeTraits<Shape>::degenerate_x ||
                  ShapeTraits<Shape>::degenerate_y ||
                  ShapeTraits<Shape>::degenerate_z) {
      return false;
    }

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
         first_value < static_cast<std::int64_t>(axis_extent(first_scan_axis));
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
  };
  if (try_plane_gap_route_3d()) {
    return PathResult{PathStatus::Found, plane_gap_cost_3d,
                      scratch.path_.size(), scratch.path_.size(),
                      scratch.path_};
  }

  auto forced_plane_gap_cost = infinite_cost;
  auto forced_plane_gap_no_path = false;
  const auto try_forced_plane_gaps_2d = [&](detail::Axis progress_axis) {
    if constexpr (!(ShapeTraits<Shape>::degenerate_x ||
                    ShapeTraits<Shape>::degenerate_y ||
                    ShapeTraits<Shape>::degenerate_z)) {
      return false;
    }
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
        request.start.*progress_member < request.goal.*progress_member ? 1 : -1;
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
           value < static_cast<std::int64_t>(axis_extent(gap_axis)); ++value) {
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
  if (request.start.y == request.goal.y && request.start.z == request.goal.z &&
      (try_axis_aligned_detour(&Coord3::x, &Coord3::y, 1) ||
       try_axis_aligned_detour(&Coord3::x, &Coord3::y, -1) ||
       try_axis_aligned_detour(&Coord3::x, &Coord3::z, 1) ||
       try_axis_aligned_detour(&Coord3::x, &Coord3::z, -1))) {
    return PathResult{PathStatus::Found, detour_cost, scratch.path_.size(),
                      scratch.path_.size(), scratch.path_};
  }
  if (request.start.x == request.goal.x && request.start.z == request.goal.z &&
      (try_axis_aligned_detour(&Coord3::y, &Coord3::x, 1) ||
       try_axis_aligned_detour(&Coord3::y, &Coord3::x, -1) ||
       try_axis_aligned_detour(&Coord3::y, &Coord3::z, 1) ||
       try_axis_aligned_detour(&Coord3::y, &Coord3::z, -1))) {
    return PathResult{PathStatus::Found, detour_cost, scratch.path_.size(),
                      scratch.path_.size(), scratch.path_};
  }
  if (request.start.x == request.goal.x && request.start.y == request.goal.y &&
      (try_axis_aligned_detour(&Coord3::z, &Coord3::x, 1) ||
       try_axis_aligned_detour(&Coord3::z, &Coord3::x, -1) ||
       try_axis_aligned_detour(&Coord3::z, &Coord3::y, 1) ||
       try_axis_aligned_detour(&Coord3::z, &Coord3::y, -1))) {
    return PathResult{PathStatus::Found, detour_cost, scratch.path_.size(),
                      scratch.path_.size(), scratch.path_};
  }

  const auto node_count = detail::tile_count<World>();
  if (scratch.state_.size() != node_count) {
    TESS_DIAG_EVENT(path_initialize);
    scratch.generation_.assign(node_count, 0);
    scratch.state_.assign(node_count, unseen);
    scratch.g_.assign(node_count, infinite_cost);
    scratch.parent_.assign(node_count, no_parent);
  }

  const auto start = detail::tile_index<Shape>(request.start);
  const auto goal = detail::tile_index<Shape>(request.goal);
  scratch.g_[static_cast<std::size_t>(start)] = 0;
  scratch.state_[static_cast<std::size_t>(start)] = open;
  scratch.touch_node(start);
  TESS_DIAG_EVENT(path_touch_node);
  TESS_DIAG_EVENT(path_heuristic);
  auto current_f = detail::manhattan(request.start, request.goal);
  scratch.open_.push_back(PathScratch::OpenNode{
      start,
      0,
      current_f,
  });
  TESS_DIAG_EVENT(path_heap_push);

  std::size_t expanded_nodes = 0;
  while (!scratch.open_.empty() || !scratch.open_next_.empty()) {
    if (scratch.open_.empty()) {
      current_f += 2;
      scratch.open_.swap(scratch.open_next_);
    }

    TESS_DIAG_EVENT(path_heap_pop);
    const auto current = scratch.open_.back();
    scratch.open_.pop_back();

    const auto current_offset = static_cast<std::size_t>(current.index);
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
        step = scratch.parent_[static_cast<std::size_t>(step)];
      }
      std::reverse(scratch.path_.begin(), scratch.path_.end());
      return PathResult{PathStatus::Found, current.g, expanded_nodes,
                        scratch.touched_.size(), scratch.path_};
    }

    const auto current_coord = detail::tile_coord<Shape>(current.index);
    detail::for_each_indexed_axis_neighbor<Shape>(
        current_coord, current.index,
        [&](Coord3 neighbor, std::uint64_t neighbor_index) {
          TESS_DIAG_EVENT(path_neighbor_candidate);
          const auto neighbor_offset = static_cast<std::size_t>(neighbor_index);
          const auto neighbor_state = scratch.state_at(neighbor_offset, unseen);
          if (neighbor_state == closed) {
            TESS_DIAG_EVENT(path_neighbor_closed);
            return;
          }
          const auto tentative_g = current.g + 1;
          TESS_DIAG_EVENT(path_relax_attempt);
          if (neighbor_state == unseen) {
            TESS_DIAG_EVENT(path_passability_check);
            if (!detail::is_passable_index<World, Tag>(world, neighbor_index)) {
              TESS_DIAG_EVENT(path_neighbor_blocked);
              return;
            }
          }
          if (tentative_g < scratch.g_at(neighbor_offset, infinite_cost)) {
            TESS_DIAG_EVENT(path_relax_success);
            if (neighbor_state == unseen) {
              scratch.touch_node(neighbor_index);
              TESS_DIAG_EVENT(path_touch_node);
            }
            scratch.g_[neighbor_offset] = tentative_g;
            scratch.parent_[neighbor_offset] = current.index;
            scratch.state_[neighbor_offset] = open;
            TESS_DIAG_EVENT(path_heuristic);
            const auto updated_node = PathScratch::OpenNode{
                neighbor_index,
                tentative_g,
                tentative_g + detail::manhattan(neighbor, request.goal),
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

  return PathResult{PathStatus::NoPath, 0, expanded_nodes,
                    scratch.touched_.size(), scratch.path_};
}

template <typename World, typename PassableTag, typename CostTag>
auto weighted_astar_path(const World& world, PathRequest request,
                         PathScratch& scratch) -> PathResult {
  using Shape = typename World::shape_type;
  constexpr auto unseen = std::uint8_t{0};
  constexpr auto open = std::uint8_t{1};
  constexpr auto closed = std::uint8_t{2};
  constexpr auto no_parent = std::numeric_limits<std::uint64_t>::max();
  constexpr auto infinite_cost = std::numeric_limits<std::uint32_t>::max();

  TESS_DIAG_EVENT_VALUE(path_clear, scratch.touched_.size());
  scratch.clear();
  if (!contains<Shape>(request.start)) {
    return PathResult{PathStatus::InvalidStart, 0, 0, 0, scratch.path_};
  }
  TESS_DIAG_EVENT(path_start_passability_check);
  if (!detail::is_passable<World, PassableTag>(world, request.start)) {
    return PathResult{PathStatus::InvalidStart, 0, 0, 0, scratch.path_};
  }
  if (!contains<Shape>(request.goal)) {
    return PathResult{PathStatus::InvalidGoal, 0, 0, 0, scratch.path_};
  }
  TESS_DIAG_EVENT(path_goal_passability_check);
  if (!detail::is_passable<World, PassableTag>(world, request.goal)) {
    return PathResult{PathStatus::InvalidGoal, 0, 0, 0, scratch.path_};
  }

  const auto start = detail::tile_index<Shape>(request.start);
  const auto goal = detail::tile_index<Shape>(request.goal);
  if (detail::tile_entry_cost_index<World, CostTag>(world, start) == 0) {
    return PathResult{PathStatus::InvalidStart, 0, 0, 0, scratch.path_};
  }
  if (detail::tile_entry_cost_index<World, CostTag>(world, goal) == 0) {
    return PathResult{PathStatus::InvalidGoal, 0, 0, 0, scratch.path_};
  }

  auto direct_current = request.start;
  auto direct_axis_blocked = false;
  const auto append_unit_axis = [&](auto Coord3::* member) {
    while (direct_current.*member != request.goal.*member) {
      direct_current.*member +=
          direct_current.*member < request.goal.*member ? 1 : -1;
      const auto direct_index = detail::tile_index<Shape>(direct_current);
      TESS_DIAG_EVENT(path_passability_check);
      if (!detail::is_passable_index<World, PassableTag>(world, direct_index)) {
        direct_axis_blocked = true;
        scratch.path_.clear();
        return false;
      }
      const auto entry_cost =
          detail::tile_entry_cost_index<World, CostTag>(world, direct_index);
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
  const auto try_unit_direct_order = [&](auto first_member, auto second_member,
                                         auto third_member) {
    direct_current = request.start;
    scratch.path_.clear();
    scratch.path_.push_back(direct_current);
    TESS_DIAG_EVENT(path_reconstruct_node);
    return append_unit_axis(first_member) && append_unit_axis(second_member) &&
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
    return PathResult{PathStatus::Found, cost, scratch.path_.size(),
                      scratch.path_.size(), scratch.path_};
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
      if (!detail::is_passable_index<World, PassableTag>(world, index)) {
        scratch.path_.clear();
        return false;
      }
      if (detail::tile_entry_cost_index<World, CostTag>(world, index) != 1) {
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
  if (request.start.y == request.goal.y && request.start.z == request.goal.z &&
      (append_unit_detour_axis(&Coord3::x, &Coord3::y, 1) ||
       append_unit_detour_axis(&Coord3::x, &Coord3::y, -1) ||
       append_unit_detour_axis(&Coord3::x, &Coord3::z, 1) ||
       append_unit_detour_axis(&Coord3::x, &Coord3::z, -1))) {
    return PathResult{PathStatus::Found, detour_cost, scratch.path_.size(),
                      scratch.path_.size(), scratch.path_};
  }
  if (request.start.x == request.goal.x && request.start.z == request.goal.z &&
      (append_unit_detour_axis(&Coord3::y, &Coord3::x, 1) ||
       append_unit_detour_axis(&Coord3::y, &Coord3::x, -1) ||
       append_unit_detour_axis(&Coord3::y, &Coord3::z, 1) ||
       append_unit_detour_axis(&Coord3::y, &Coord3::z, -1))) {
    return PathResult{PathStatus::Found, detour_cost, scratch.path_.size(),
                      scratch.path_.size(), scratch.path_};
  }
  if (request.start.x == request.goal.x && request.start.y == request.goal.y &&
      (append_unit_detour_axis(&Coord3::z, &Coord3::x, 1) ||
       append_unit_detour_axis(&Coord3::z, &Coord3::x, -1) ||
       append_unit_detour_axis(&Coord3::z, &Coord3::y, 1) ||
       append_unit_detour_axis(&Coord3::z, &Coord3::y, -1))) {
    return PathResult{PathStatus::Found, detour_cost, scratch.path_.size(),
                      scratch.path_.size(), scratch.path_};
  }

  const auto node_count = detail::tile_count<World>();
  if (scratch.state_.size() != node_count) {
    TESS_DIAG_EVENT(path_initialize);
    scratch.generation_.assign(node_count, 0);
    scratch.state_.assign(node_count, unseen);
    scratch.g_.assign(node_count, infinite_cost);
    scratch.parent_.assign(node_count, no_parent);
  }

  scratch.g_[static_cast<std::size_t>(start)] = 0;
  scratch.state_[static_cast<std::size_t>(start)] = open;
  scratch.touch_node(start);
  TESS_DIAG_EVENT(path_touch_node);
  TESS_DIAG_EVENT(path_heuristic);
  scratch.open_.push_back(PathScratch::OpenNode{
      start,
      0,
      detail::manhattan(request.start, request.goal),
  });
  std::push_heap(scratch.open_.begin(), scratch.open_.end(),
                 detail::open_node_less);
  TESS_DIAG_EVENT(path_heap_push);

  std::size_t expanded_nodes = 0;
  while (!scratch.open_.empty()) {
    TESS_DIAG_EVENT(path_heap_pop);
    std::pop_heap(scratch.open_.begin(), scratch.open_.end(),
                  detail::open_node_less);
    const auto current = scratch.open_.back();
    scratch.open_.pop_back();

    const auto current_offset = static_cast<std::size_t>(current.index);
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
        step = scratch.parent_[static_cast<std::size_t>(step)];
      }
      std::reverse(scratch.path_.begin(), scratch.path_.end());
      return PathResult{PathStatus::Found, current.g, expanded_nodes,
                        scratch.touched_.size(), scratch.path_};
    }

    const auto current_coord = detail::tile_coord<Shape>(current.index);
    detail::for_each_indexed_axis_neighbor<Shape>(
        current_coord, current.index,
        [&](Coord3 neighbor, std::uint64_t neighbor_index) {
          TESS_DIAG_EVENT(path_neighbor_candidate);
          const auto neighbor_offset = static_cast<std::size_t>(neighbor_index);
          const auto neighbor_state = scratch.state_at(neighbor_offset, unseen);
          if (neighbor_state == closed) {
            TESS_DIAG_EVENT(path_neighbor_closed);
            return;
          }
          TESS_DIAG_EVENT(path_relax_attempt);
          if (neighbor_state == unseen) {
            TESS_DIAG_EVENT(path_passability_check);
            if (!detail::is_passable_index<World, PassableTag>(
                    world, neighbor_index)) {
              TESS_DIAG_EVENT(path_neighbor_blocked);
              return;
            }
          }

          const auto entry_cost = detail::tile_entry_cost_index<World, CostTag>(
              world, neighbor_index);
          if (entry_cost == 0) {
            TESS_DIAG_EVENT(path_neighbor_blocked);
            return;
          }
          const auto tentative_g =
              detail::saturating_add(current.g, entry_cost);
          if (tentative_g == infinite_cost) {
            return;
          }
          if (tentative_g < scratch.g_at(neighbor_offset, infinite_cost)) {
            TESS_DIAG_EVENT(path_relax_success);
            if (neighbor_state == unseen) {
              scratch.touch_node(neighbor_index);
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
                    tentative_g, detail::manhattan(neighbor, request.goal)),
            });
            std::push_heap(scratch.open_.begin(), scratch.open_.end(),
                           detail::open_node_less);
            TESS_DIAG_EVENT(path_heap_push);
          }
        });
  }

  return PathResult{PathStatus::NoPath, 0, expanded_nodes,
                    scratch.touched_.size(), scratch.path_};
}

template <typename World, typename PassableTag, typename CostTag>
auto build_weighted_route_product(const World& world, PathRequest request,
                                  PathScratch& scratch,
                                  WeightedRouteProduct& product) -> PathResult {
  using Shape = typename World::shape_type;

  product.clear();
  const auto result =
      weighted_astar_path<World, PassableTag, CostTag>(world, request, scratch);
  product.request_ = request;
  product.status_ = result.status;
  product.cost_ = result.cost;
  product.expanded_nodes_ = result.expanded_nodes;
  product.reached_nodes_ = result.reached_nodes;
  product.path_.assign(result.path.begin(), result.path.end());
  for (const auto coord : product.path_) {
    const auto key = tile_key<Shape>(coord);
    product.dependencies_.add_chunk(world, chunk_key<Shape>(key));
  }

  return PathResult{product.status_, product.cost_, product.expanded_nodes_,
                    product.reached_nodes_, product.path_};
}

template <typename World>
auto weighted_route_product_path(const World& world,
                                 const WeightedRouteProduct& product)
    -> PathResult {
  if (!product.is_valid(world)) {
    return PathResult{PathStatus::NoPath, 0, 0, 0, {}};
  }
  return PathResult{product.status_, product.cost_, 0, 0, product.path_};
}

template <typename World, typename PassableTag, typename CostTag>
auto build_weighted_portal_route_product(const World& world,
                                         PathRequest request,
                                         std::span<const Coord3> waypoints,
                                         PathScratch& scratch,
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

template <typename World>
auto weighted_portal_route_product_path(
    const World& world, const WeightedPortalRouteProduct& product)
    -> PathResult {
  if (!product.is_valid(world)) {
    return PathResult{PathStatus::NoPath, 0, 0, 0, {}};
  }
  return PathResult{product.status_, product.cost_, 0, 0, product.path_};
}

template <typename World, typename Tag>
auto build_distance_field(const World& world, Coord3 goal,
                          DistanceFieldScratch& scratch)
    -> DistanceFieldResult {
  using Shape = typename World::shape_type;
  constexpr auto infinite_distance = std::numeric_limits<std::uint32_t>::max();

  TESS_DIAG_EVENT_VALUE(path_clear, scratch.touched_.size());
  scratch.clear_build();
  if (!contains<Shape>(goal)) {
    return DistanceFieldResult{PathStatus::InvalidGoal, 0, 0};
  }
  TESS_DIAG_EVENT(path_goal_passability_check);
  if (!detail::is_passable<World, Tag>(world, goal)) {
    return DistanceFieldResult{PathStatus::InvalidGoal, 0, 0};
  }

  const auto node_count = detail::tile_count<World>();
  if (scratch.distance_.size() != node_count) {
    TESS_DIAG_EVENT(path_initialize);
    scratch.generation_.assign(node_count, 0);
    scratch.distance_.assign(node_count, infinite_distance);
  }

  const auto goal_index = detail::tile_index<Shape>(goal);
  scratch.goal_ = goal;
  scratch.has_goal_ = true;
  scratch.distance_[static_cast<std::size_t>(goal_index)] = 0;
  scratch.touch_node(goal_index);
  TESS_DIAG_EVENT(path_touch_node);
  scratch.frontier_.push_back(goal_index);
  TESS_DIAG_EVENT(path_heap_push);

  std::size_t expanded_nodes = 0;
  std::size_t head = 0;
  while (head < scratch.frontier_.size()) {
    const auto current = scratch.frontier_[head];
    ++head;
    TESS_DIAG_EVENT(path_heap_pop);
    ++expanded_nodes;

    const auto current_offset = static_cast<std::size_t>(current);
    const auto current_distance =
        scratch.distance_at(current_offset, infinite_distance);
    const auto current_coord = detail::tile_coord<Shape>(current);
    detail::for_each_indexed_axis_neighbor<Shape>(
        current_coord, current, [&](Coord3, std::uint64_t neighbor_index) {
          TESS_DIAG_EVENT(path_neighbor_candidate);
          const auto neighbor_offset = static_cast<std::size_t>(neighbor_index);
          if (scratch.is_current(neighbor_offset)) {
            TESS_DIAG_EVENT(path_neighbor_closed);
            return;
          }
          TESS_DIAG_EVENT(path_passability_check);
          if (!detail::is_passable_index<World, Tag>(world, neighbor_index)) {
            TESS_DIAG_EVENT(path_neighbor_blocked);
            return;
          }

          scratch.distance_[neighbor_offset] = current_distance + 1;
          scratch.touch_node(neighbor_index);
          TESS_DIAG_EVENT(path_touch_node);
          scratch.frontier_.push_back(neighbor_index);
          TESS_DIAG_EVENT(path_heap_push);
        });
  }

  return DistanceFieldResult{PathStatus::Found, expanded_nodes,
                             scratch.touched_.size()};
}

template <typename World, typename Tag>
auto distance_field_path(const World& world, Coord3 start, Coord3 goal,
                         DistanceFieldScratch& scratch) -> PathResult {
  using Shape = typename World::shape_type;
  constexpr auto infinite_distance = std::numeric_limits<std::uint32_t>::max();

  scratch.clear_path();
  if (!contains<Shape>(start)) {
    return PathResult{PathStatus::InvalidStart, 0, 0, 0, scratch.path_};
  }
  TESS_DIAG_EVENT(path_start_passability_check);
  if (!detail::is_passable<World, Tag>(world, start)) {
    return PathResult{PathStatus::InvalidStart, 0, 0, 0, scratch.path_};
  }
  if (!contains<Shape>(goal)) {
    return PathResult{PathStatus::InvalidGoal, 0, 0, 0, scratch.path_};
  }
  if (!scratch.has_goal_ || scratch.goal_ != goal) {
    return PathResult{PathStatus::NoPath, 0, 0, 0, scratch.path_};
  }

  const auto start_index = detail::tile_index<Shape>(start);
  auto current = start_index;
  auto current_offset = static_cast<std::size_t>(current);
  auto current_distance =
      scratch.distance_at(current_offset, infinite_distance);
  if (current_distance == infinite_distance) {
    return PathResult{PathStatus::NoPath, 0, 0, scratch.touched_.size(),
                      scratch.path_};
  }

  scratch.path_.push_back(start);
  TESS_DIAG_EVENT(path_reconstruct_node);
  while (current_distance > 0) {
    const auto current_coord = detail::tile_coord<Shape>(current);
    auto next = current;
    auto next_distance = current_distance;
    detail::for_each_indexed_axis_neighbor<Shape>(
        current_coord, current, [&](Coord3, std::uint64_t neighbor_index) {
          const auto neighbor_offset = static_cast<std::size_t>(neighbor_index);
          const auto neighbor_distance =
              scratch.distance_at(neighbor_offset, infinite_distance);
          if (neighbor_distance < next_distance) {
            next = neighbor_index;
            next_distance = neighbor_distance;
          }
        });

    if (next == current || next_distance + 1 != current_distance) {
      scratch.path_.clear();
      return PathResult{PathStatus::NoPath, 0, 0, scratch.touched_.size(),
                        scratch.path_};
    }

    current = next;
    current_offset = static_cast<std::size_t>(current);
    current_distance = scratch.distance_at(current_offset, infinite_distance);
    scratch.path_.push_back(detail::tile_coord<Shape>(current));
    TESS_DIAG_EVENT(path_reconstruct_node);
  }

  return PathResult{PathStatus::Found,
                    scratch.distance_[static_cast<std::size_t>(start_index)],
                    scratch.path_.size(), scratch.touched_.size(),
                    scratch.path_};
}

template <typename World, typename PassableTag, typename CostTag>
auto build_weighted_distance_field(const World& world, Coord3 goal,
                                   DistanceFieldScratch& scratch)
    -> DistanceFieldResult {
  using Shape = typename World::shape_type;
  constexpr auto infinite_distance = std::numeric_limits<std::uint32_t>::max();

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

  scratch.goal_ = goal;
  scratch.has_goal_ = true;
  scratch.distance_[static_cast<std::size_t>(goal_index)] = 0;
  scratch.touch_node(goal_index);
  TESS_DIAG_EVENT(path_touch_node);
  scratch.weighted_frontier_.push_back(PathScratch::OpenNode{goal_index, 0, 0});
  std::push_heap(scratch.weighted_frontier_.begin(),
                 scratch.weighted_frontier_.end(), detail::open_node_less);
  TESS_DIAG_EVENT(path_heap_push);

  std::size_t expanded_nodes = 0;
  while (!scratch.weighted_frontier_.empty()) {
    TESS_DIAG_EVENT(path_heap_pop);
    std::pop_heap(scratch.weighted_frontier_.begin(),
                  scratch.weighted_frontier_.end(), detail::open_node_less);
    const auto current = scratch.weighted_frontier_.back();
    scratch.weighted_frontier_.pop_back();

    const auto current_offset = static_cast<std::size_t>(current.index);
    const auto current_distance =
        scratch.distance_at(current_offset, infinite_distance);
    if (current.g != current_distance) {
      TESS_DIAG_EVENT_VALUE(path_skip_pop, false);
      continue;
    }
    ++expanded_nodes;

    const auto current_entry_cost =
        detail::tile_entry_cost_index<World, CostTag>(world, current.index);
    if (current_entry_cost == 0) {
      continue;
    }
    const auto current_coord = detail::tile_coord<Shape>(current.index);
    detail::for_each_indexed_axis_neighbor<Shape>(
        current_coord, current.index,
        [&](Coord3, std::uint64_t neighbor_index) {
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
            scratch.weighted_frontier_.push_back(PathScratch::OpenNode{
                neighbor_index,
                next_distance,
                next_distance,
            });
            std::push_heap(scratch.weighted_frontier_.begin(),
                           scratch.weighted_frontier_.end(),
                           detail::open_node_less);
            TESS_DIAG_EVENT(path_heap_push);
          }
        });
  }

  return DistanceFieldResult{PathStatus::Found, expanded_nodes,
                             scratch.touched_.size()};
}

#include <tess/path/detail/weighted_batch.h>

}  // namespace tess

#include <tess/path/route_cache.h>
