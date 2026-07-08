#pragma once

#define TESS_PATH_PATH_H_INCLUDED 1

#include <tess/core/shape.h>
#include <tess/diagnostics/diagnostics.h>
#include <tess/path/node_index_space.h>

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
  // Sparse worlds only: the search reached the edge of the resident set and
  // could not rule out a path through a non-resident chunk. Distinguished from
  // NoPath so a caller never mistakes "not searched" for "no route exists" and
  // can materialize the missing chunks and retry.
  Indeterminate,
};
static_assert(sizeof(PathStatus) == sizeof(std::uint8_t));

// How a search treats a step into a non-resident chunk of a sparse world.
// Inert for dense worlds, where every chunk is resident.
enum class MissingChunkPolicy : std::uint8_t {
  // Treat a non-resident chunk as impassable. The search stays within the
  // resident set and may report NoPath even when a route exists through
  // chunks that are not currently materialized.
  TreatAsBlocked,
  // Never report a wrong NoPath across a non-resident boundary: if the search
  // exhausts the resident set having skipped at least one non-resident
  // neighbor, it returns Indeterminate instead of NoPath.
  Indeterminate,
};

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

// Declared here so the MissingChunkPolicy default lives on the first
// declaration; the friend declarations and definitions below omit it.
template <typename World, typename Tag>
auto astar_path(const World& world, PathRequest request, PathScratch& scratch,
                MissingChunkPolicy policy = MissingChunkPolicy::TreatAsBlocked)
    -> PathResult;

template <typename World, typename PassableTag, typename CostTag>
auto weighted_astar_path(
    const World& world, PathRequest request, PathScratch& scratch,
    MissingChunkPolicy policy = MissingChunkPolicy::TreatAsBlocked)
    -> PathResult;

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
                         PathScratch& scratch, MissingChunkPolicy policy)
      -> PathResult;

  template <typename World, typename PassableTag, typename CostTag>
  friend auto weighted_astar_path(const World& world, PathRequest request,
                                  PathScratch& scratch,
                                  MissingChunkPolicy policy) -> PathResult;

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

  // offset is the node-array slot for index under the search's
  // NodeIndexSpace; index is the global tile index recorded for the
  // expansion metric. For the dense world offset == index.
  void touch_node(std::size_t offset, std::uint64_t index) {
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
    request_goal_.reserve(request_count);
    goal_coords_.reserve(request_count);
    goal_counts_.reserve(request_count);
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
  // Reusable goal -> request count flat map (open-addressed, power-of-two
  // capacity, linear probing) built once per batch call.
  std::vector<std::uint32_t> goal_slots_;
  std::vector<Coord3> goal_coords_;
  std::vector<std::uint32_t> goal_counts_;
  std::vector<std::uint32_t> request_goal_;
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

// FNV-style lane combine with one final avalanche: cheap per coordinate,
// well distributed for the power-of-two linear-probing flat hash maps used
// by the batch planner and the request runtime (matching the route cache's
// hashing style).
[[nodiscard]] constexpr auto coord_hash(Coord3 coord) noexcept
    -> std::uint64_t {
  auto hash = std::uint64_t{0xcbf29ce484222325ull};
  hash = (hash ^ static_cast<std::uint64_t>(coord.x)) * 0x100000001b3ull;
  hash = (hash ^ static_cast<std::uint64_t>(coord.y)) * 0x100000001b3ull;
  hash = (hash ^ static_cast<std::uint64_t>(coord.z)) * 0x100000001b3ull;
  hash = (hash ^ (hash >> 30u)) * 0xbf58476d1ce4e5b9ull;
  hash = (hash ^ (hash >> 27u)) * 0x94d049bb133111ebull;
  return hash ^ (hash >> 31u);
}

[[nodiscard]] constexpr auto manhattan(Coord3 lhs, Coord3 rhs) noexcept
    -> std::uint32_t {
  const auto distance = manhattan_distance(lhs, rhs);
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
  if constexpr (std::is_same_v<typename World::residency_type,
                               SparseResident>) {
    // A non-resident chunk carries no data, so it reads as impassable. This
    // keeps the unchecked hot-loop access safe on sparse worlds; the search's
    // residency guard decides whether "impassable" means blocked or missing.
    const auto* page = world.try_chunk(chunk_key<Shape>(key));
    if (page == nullptr) {
      return false;
    }
    return static_cast<bool>(
        page->template field<Tag>(local_tile_id<Shape>(key)));
  } else {
    const auto& value = world.chunk(chunk_key<Shape>(key))
                            .template field<Tag>(local_tile_id<Shape>(key));
    return static_cast<bool>(value);
  }
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

#include <tess/path/detail/astar.h>

template <typename World, typename PassableTag, typename CostTag>
auto build_weighted_route_product(const World& world, PathRequest request,
                                  PathScratch& scratch,
                                  WeightedRouteProduct& product) -> PathResult {
  using Shape = typename World::shape_type;
  // Route products track chunk-version dependencies for cached replay
  // (weighted_route_product_path -> is_valid), which reads meta() for chunks a
  // sparse world may have since evicted. Dense-only until the sparse
  // route-cache slice defines dependency validity under eviction; weighted A*
  // itself runs natively on sparse worlds via weighted_astar_path.
  static_assert(
      std::is_same_v<typename World::residency_type, AlwaysResident>,
      "build_weighted_route_product is dense-only; call weighted_astar_path "
      "directly for sparse worlds, or await the sparse route-cache slice.");

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
  // Same chunk-version dependency tracking as build_weighted_route_product, so
  // dense-only until the sparse route-cache slice. The per-segment weighted A*
  // it chains already supports sparse worlds via weighted_astar_path.
  static_assert(
      std::is_same_v<typename World::residency_type, AlwaysResident>,
      "build_weighted_portal_route_product is dense-only; chain "
      "weighted_astar_path directly for sparse worlds, or await the sparse "
      "route-cache slice.");

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
  // Distance fields size their node arrays by the global tile count and index
  // them by raw tile id; on a sparse world that would allocate for the entire
  // (possibly astronomical) shape and index out of bounds. Guarded dense-only
  // until the distance-field family is ported to NodeIndexSpace.
  static_assert(
      std::is_same_v<typename World::residency_type, AlwaysResident>,
      "build_distance_field does not yet support sparse (SparseResident) "
      "worlds; the sparse distance-field slice lands later.");
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
  // See build_distance_field: dense-only until the distance-field family is
  // ported to NodeIndexSpace.
  static_assert(
      std::is_same_v<typename World::residency_type, AlwaysResident>,
      "build_weighted_distance_field does not yet support sparse "
      "(SparseResident) worlds; the sparse distance-field slice lands later.");
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
