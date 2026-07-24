#pragma once

#define TESS_PATH_PATH_H_INCLUDED 1

#include <tess/core/shape.h>
#include <tess/core/tag_identity.h>
#include <tess/diagnostics/diagnostics.h>
#include <tess/path/node_index_space.h>
#include <tess/path/path_view.h>
#include <tess/topology/movement_class.h>
#include <tess/topology/transition_model.h>

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace tess {

/// Classifies a path query result or a conservative incomplete search.
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
  // A legal route step or accumulated exact cost reached the reserved
  // uint32_t infinity sentinel and therefore cannot be represented.
  CostOverflow,
};
static_assert(sizeof(PathStatus) == sizeof(std::uint8_t));

// How a search treats a step into a non-resident chunk of a sparse world.
// Inert for dense worlds, where every chunk is resident.
/// Selects how sparse searches report boundaries of the resident set.
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

/// Specifies inclusive start and goal coordinates for a path query.
struct PathRequest {
  Coord3 start;
  Coord3 goal;
};

/// Reports a query outcome and a non-owning view of the resulting path.
///
/// The path normally borrows caller-owned scratch or product storage and is
/// invalidated by that owner's next mutation.
struct PathResult {
  PathStatus status = PathStatus::NoPath;
  std::uint32_t cost = 0;
  std::size_t expanded_nodes = 0;
  std::size_t reached_nodes = 0;
  PathView path;
  std::uint32_t cost_scale = 1;
};

/// Reports distance-field construction status and search work.
struct DistanceFieldResult {
  PathStatus status = PathStatus::NoPath;
  std::size_t expanded_nodes = 0;
  std::size_t reached_nodes = 0;
};

/// Summarizes grouping, field reuse, fallbacks, and output for a weighted
/// batch.
struct WeightedPathBatchStats {
  std::size_t requests = 0;
  std::size_t unique_goals = 0;
  std::size_t field_builds = 0;
  std::size_t astar_fallbacks = 0;
  std::size_t path_nodes = 0;
};

/// Owns reusable node and result storage for A* queries.
class PathScratch;
/// Owns reusable node and path storage for distance-field queries.
class DistanceFieldScratch;
/// Owns the ordered goals for a multi-goal distance product.
class GoalSet;
/// Owns a reusable multi-goal distance field and dependency snapshot.
class DistanceFieldProduct;
/// Owns reusable route-cache entries and their query scratch.
class RouteCacheScratch;
/// Owns reusable grouping, search, and output storage for weighted batches.
class WeightedPathBatchScratch;
/// Reports the closest goal and a borrowed path to it.
struct NearestTargetResult;

namespace detail {
// Core behind weighted_distance_field_path; verify_residency lets
// weighted_path_batch skip the O(resident_count) fingerprint recompute for
// fields it reads against the same const world it just built them from.
template <typename World, typename Class>
auto weighted_distance_field_path_core(const World& world, Coord3 start,
                                       Coord3 goal,
                                       DistanceFieldScratch& scratch,
                                       bool verify_residency) -> PathResult;

template <typename World, typename Class, typename Provider>
auto weighted_distance_field_path_core(const World& world, Coord3 start,
                                       Coord3 goal,
                                       DistanceFieldScratch& scratch,
                                       bool verify_residency,
                                       const Provider& provider) -> PathResult;

// Core behind build_bounded_weighted_distance_field. settle_targets are
// validated tile indices whose distances the caller will read; once every
// target is settled the flood stops instead of exhausting the reachable
// component (audit 2026-07-11 M3). Empty span = flood to exhaustion (the
// public wrapper's behavior, byte-identical to the pre-M3 build).
template <typename World, typename Class, std::uint32_t MaxCost>
auto build_bounded_weighted_distance_field_core(
    const World& world, Coord3 goal, DistanceFieldScratch& scratch,
    MissingChunkPolicy policy, std::span<const std::uint64_t> settle_targets)
    -> DistanceFieldResult;
}  // namespace detail

// Declared here so the MissingChunkPolicy default lives on the first
// declaration; the friend declarations and definitions below omit it.
/// Finds a minimum-step path using a truthy passability field.
///
/// The returned path borrows `scratch` until its next mutation. Invalid
/// endpoints and exhausted searches are distinguished in `PathStatus`.
template <typename World, typename Tag>
auto astar_path(const World& world, PathRequest request, PathScratch& scratch,
                MissingChunkPolicy policy = MissingChunkPolicy::TreatAsBlocked)
    -> PathResult;

/// Finds a minimum-step path composed with a special-transition provider.
template <typename World, typename Tag, typename Provider>
auto astar_path(const World& world, PathRequest request, PathScratch& scratch,
                MissingChunkPolicy policy, const Provider& provider)
    -> PathResult;

// The weighted searches come in two forms: the core takes one MovementClass
// fusing passability and entry cost; the legacy <PassableTag, CostTag> pair
// forwards through movement::LegacyWeighted (identical semantics, including
// the cost-agnostic passability asymmetry).
/// Finds a minimum-cost path for a compile-time movement class.
///
/// The returned path borrows `scratch` until its next mutation.
template <typename World, typename Class>
auto weighted_astar_path(
    const World& world, PathRequest request, PathScratch& scratch,
    MissingChunkPolicy policy = MissingChunkPolicy::TreatAsBlocked)
    -> PathResult;

/// Finds a weighted path composed with a special-transition provider.
template <typename World, typename Class, typename Provider>
auto weighted_astar_path(const World& world, PathRequest request,
                         PathScratch& scratch, MissingChunkPolicy policy,
                         const Provider& provider) -> PathResult;

template <typename World, typename PassableTag, typename CostTag>
/// Finds a weighted path using separate legacy passability and cost tags.
auto weighted_astar_path(
    const World& world, PathRequest request, PathScratch& scratch,
    MissingChunkPolicy policy = MissingChunkPolicy::TreatAsBlocked)
    -> PathResult;

/// Builds a dense multi-goal field into caller-owned reusable storage.
template <typename World, typename Tag>
auto build_distance_field_product(const World& world, const GoalSet& goals,
                                  DistanceFieldScratch& scratch,
                                  DistanceFieldProduct& product)
    -> DistanceFieldResult;

/// Builds a dense multi-goal field composed with a special provider.
template <typename World, typename Tag, typename Provider>
auto build_distance_field_product(const World& world, const GoalSet& goals,
                                  DistanceFieldScratch& scratch,
                                  DistanceFieldProduct& product,
                                  const Provider& provider)
    -> DistanceFieldResult;

/// Reconstructs a borrowed path from a valid multi-goal product.
template <typename World, typename Tag>
auto distance_field_product_path(const World& world, Coord3 start,
                                 const DistanceFieldProduct& product,
                                 DistanceFieldScratch& scratch) -> PathResult;

/// Reads a multi-goal product through its matching special provider.
template <typename World, typename Tag, typename Provider>
auto distance_field_product_path(const World& world, Coord3 start,
                                 const DistanceFieldProduct& product,
                                 DistanceFieldScratch& scratch,
                                 const Provider& provider) -> PathResult;

/// Finds the nearest reachable goal represented by a valid product.
template <typename World, typename Tag>
auto nearest_target(const World& world, Coord3 start,
                    const DistanceFieldProduct& product,
                    DistanceFieldScratch& scratch) -> NearestTargetResult;

/// Builds an unweighted field rooted at `goal` into reusable scratch storage.
///
/// The build may allocate unless scratch was reserved. Sparse residency
/// boundaries follow `policy` and are reported conservatively.
template <typename WorldType, typename Tag>
auto build_distance_field(
    const WorldType& world, Coord3 goal, DistanceFieldScratch& scratch,
    MissingChunkPolicy policy = MissingChunkPolicy::TreatAsBlocked)
    -> DistanceFieldResult;

/// Builds an unweighted reverse field composed with a special provider.
template <typename WorldType, typename Tag, typename Provider>
auto build_distance_field(const WorldType& world, Coord3 goal,
                          DistanceFieldScratch& scratch,
                          MissingChunkPolicy policy, const Provider& provider)
    -> DistanceFieldResult;

/// Builds a movement-class weighted field rooted at `goal`.
///
/// The build may allocate unless scratch was reserved. Sparse boundaries
/// follow `policy` and zero entry cost is impassable.
template <typename WorldType, typename Class>
auto build_weighted_distance_field(
    const WorldType& world, Coord3 goal, DistanceFieldScratch& scratch,
    MissingChunkPolicy policy = MissingChunkPolicy::TreatAsBlocked)
    -> DistanceFieldResult;

/// Builds a weighted reverse field composed with a special provider.
template <typename WorldType, typename Class, typename Provider>
auto build_weighted_distance_field(const WorldType& world, Coord3 goal,
                                   DistanceFieldScratch& scratch,
                                   MissingChunkPolicy policy,
                                   const Provider& provider)
    -> DistanceFieldResult;

template <typename World, typename PassableTag, typename CostTag>
/// Builds a weighted field using legacy passability and cost tags.
auto build_weighted_distance_field(
    const World& world, Coord3 goal, DistanceFieldScratch& scratch,
    MissingChunkPolicy policy = MissingChunkPolicy::TreatAsBlocked)
    -> DistanceFieldResult;

/// Builds a weighted field restricted to the intersection with `domain`.
template <typename World, typename Class>
auto build_weighted_distance_field_in_box(
    const World& world, Coord3 goal, Box3 domain, DistanceFieldScratch& scratch,
    MissingChunkPolicy policy = MissingChunkPolicy::TreatAsBlocked)
    -> DistanceFieldResult;

/// Builds a boxed weighted field composed with a special provider.
template <typename World, typename Class, typename Provider>
auto build_weighted_distance_field_in_box(const World& world, Coord3 goal,
                                          Box3 domain,
                                          DistanceFieldScratch& scratch,
                                          MissingChunkPolicy policy,
                                          const Provider& provider)
    -> DistanceFieldResult;

template <typename World, typename PassableTag, typename CostTag>
/// Builds a boxed weighted field using legacy passability and cost tags.
auto build_weighted_distance_field_in_box(
    const World& world, Coord3 goal, Box3 domain, DistanceFieldScratch& scratch,
    MissingChunkPolicy policy = MissingChunkPolicy::TreatAsBlocked)
    -> DistanceFieldResult;

/// Builds a weighted field using bounded-cost buckets when costs permit.
template <typename World, typename Class, std::uint32_t MaxCost>
auto build_bounded_weighted_distance_field(
    const World& world, Coord3 goal, DistanceFieldScratch& scratch,
    MissingChunkPolicy policy = MissingChunkPolicy::TreatAsBlocked)
    -> DistanceFieldResult;

/// Builds a bounded-cost field composed with a special provider.
template <typename World, typename Class, std::uint32_t MaxCost,
          typename Provider>
auto build_bounded_weighted_distance_field(const World& world, Coord3 goal,
                                           DistanceFieldScratch& scratch,
                                           MissingChunkPolicy policy,
                                           const Provider& provider)
    -> DistanceFieldResult;

template <typename World, typename PassableTag, typename CostTag,
          std::uint32_t MaxCost>
/// Builds a bounded weighted field with legacy passability/cost semantics.
auto build_bounded_weighted_distance_field(
    const World& world, Coord3 goal, DistanceFieldScratch& scratch,
    MissingChunkPolicy policy = MissingChunkPolicy::TreatAsBlocked)
    -> DistanceFieldResult;

/// Captures chunk versions used to reject stale cached path products.
///
/// The object owns its dependency list and may allocate unless reserved.
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
    // Keys are unique by construction: append directly instead of paying
    // add_chunk's duplicate scan per key (which would be quadratic here).
    for (std::uint64_t i = 0; i < World::chunk_count; ++i) {
      chunks_.push_back(
          ChunkVersionDependency{ChunkKey{i}, world.meta(ChunkKey{i}).version});
    }
  }

  // Appends without add_chunk's duplicate scan; the caller must guarantee
  // `key` is not already present (e.g. tracked via an external seen set).
  template <typename World>
  void add_chunk_unique(const World& world, ChunkKey key) {
    chunks_.push_back(ChunkVersionDependency{key, world.meta(key).version});
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

  [[nodiscard]] auto empty() const noexcept -> bool { return chunks_.empty(); }

  [[nodiscard]] auto chunks() const noexcept
      -> std::span<const ChunkVersionDependency> {
    return chunks_;
  }

 private:
  std::vector<ChunkVersionDependency> chunks_;
};

namespace detail {

// Failure-dependency capture shared by the route/portal product builders.
// NoPath depends on world content the search may never have touched (an
// opening edit lands on a blocked tile; fast-path early-outs sample barriers
// far from any expanded node), so precise capture is impractical: depend on
// every chunk, making any edit invalidate the replay instead of it repeating
// a stale failure forever. InvalidStart/InvalidGoal depend only on the
// offending tiles; an out-of-bounds tile contributes nothing (bounds are
// compile-time), which can leave the set empty -- the product is then
// permanently invalid and callers rebuild, paying only the cheap bounds
// rejection.
template <typename Shape, typename World>
void capture_failure_dependencies(const World& world, PathRequest request,
                                  PathStatus status,
                                  ChunkVersionDependencies& dependencies) {
  if (status == PathStatus::NoPath) {
    dependencies.capture_all(world);
    return;
  }
  if (contains<Shape>(request.start)) {
    dependencies.add_chunk(world,
                           chunk_key<Shape>(tile_key<Shape>(request.start)));
  }
  if (contains<Shape>(request.goal)) {
    dependencies.add_chunk(world,
                           chunk_key<Shape>(tile_key<Shape>(request.goal)));
  }
}

}  // namespace detail

/// Owns a weighted route and the chunk versions required to replay it.
///
/// Returned path views borrow this product and remain valid until mutation.
class WeightedRouteProduct {
 public:
  void reserve_path_nodes(std::size_t node_count) { path_.reserve(node_count); }

  // Dependency sets are bounded by the world's chunk count (failure
  // products capture every chunk): reserve chunk_count to keep steady-state
  // rebuilds allocation-free.
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

  // An empty dependency set means "never validated", not "depends on
  // nothing": cleared products and failure products that predate dependency
  // capture must never replay as vacuously valid. Builders capture_all()
  // for non-Found results, so any built product carries dependencies.
  template <typename World>
  [[nodiscard]] auto is_valid(const World& world) const noexcept -> bool {
    return !dependencies_.empty() && dependencies_.is_valid(world);
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

/// Caches weighted A* segments used to assemble portal-route products.
class WeightedPortalSegmentCache;

/// Owns a segmented weighted route, waypoints, and replay dependencies.
///
/// Returned path and waypoint views borrow this product until its next
/// mutation. Reserve capacity to keep repeated builds allocation-free.
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

  // Dependency sets are bounded by the world's chunk count (failure
  // products capture every chunk): reserve chunk_count to keep steady-state
  // rebuilds allocation-free.
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

  // See WeightedRouteProduct::is_valid: empty dependencies are invalid by
  // definition so failure/cleared products never replay vacuously.
  template <typename World>
  [[nodiscard]] auto is_valid(const World& world) const noexcept -> bool {
    return !dependencies_.empty() && dependencies_.is_valid(world);
  }

  [[nodiscard]] auto request() const noexcept -> PathRequest {
    return request_;
  }

  [[nodiscard]] auto waypoints() const noexcept -> std::span<const Coord3> {
    return waypoints_;
  }

  [[nodiscard]] auto dependencies() const noexcept
      -> std::span<const ChunkVersionDependencies::ChunkVersionDependency> {
    return dependencies_.chunks();
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

  // Rebuild calls may pass spans into this product's own storage --
  // waypoints() or a previously returned PathResult.path -- and clear() plus
  // segment stitching invalidate those. Copy product-owned input to `stash`
  // first; the copy allocates, but only on the aliased rebuild path.
  [[nodiscard]] auto stash_if_owned(std::span<const Coord3> input,
                                    std::vector<Coord3>& stash) const
      -> std::span<const Coord3> {
    const auto owned = [&](const std::vector<Coord3>& storage) {
      const auto* begin = storage.data();
      const auto* end = begin + storage.size();
      return !input.empty() &&
             !std::less<const Coord3*>{}(input.data(), begin) &&
             std::less<const Coord3*>{}(input.data(), end);
    };
    if (owned(waypoints_) || owned(path_) || owned(segment_) ||
        owned(candidate_waypoints_) || owned(best_waypoints_)) {
      stash.assign(input.begin(), input.end());
      return std::span<const Coord3>{stash};
    }
    return input;
  }

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

/// Owns reusable A* frontier, node state, and returned path storage.
///
/// Instances are caller-owned and require external synchronization. Reserving
/// for the search space avoids allocation once warm.
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
    path_.reserve(node_count);
  }

  void clear() noexcept {
    advance_epoch();
    open_.clear();
    open_next_.clear();
    touched_count_ = 0;
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

  template <typename World, typename Tag, typename Provider>
  friend auto astar_path(const World& world, PathRequest request,
                         PathScratch& scratch, MissingChunkPolicy policy,
                         const Provider& provider) -> PathResult;

  template <typename World, typename Class>
  friend auto weighted_astar_path(const World& world, PathRequest request,
                                  PathScratch& scratch,
                                  MissingChunkPolicy policy) -> PathResult;

  template <typename World, typename Class, typename Provider>
  friend auto weighted_astar_path(const World& world, PathRequest request,
                                  PathScratch& scratch,
                                  MissingChunkPolicy policy,
                                  const Provider& provider) -> PathResult;

  template <typename World, typename PassableTag, typename CostTag>
  friend auto weighted_astar_path(const World& world, PathRequest request,
                                  PathScratch& scratch,
                                  MissingChunkPolicy policy) -> PathResult;

  template <typename World, typename Tag>
  friend auto cached_astar_path(const World& world, PathRequest request,
                                PathScratch& scratch, RouteCacheScratch& cache)
      -> PathResult;

  template <typename World, typename Tag, typename Provider>
  friend auto cached_astar_path(const World& world, PathRequest request,
                                PathScratch& scratch, RouteCacheScratch& cache,
                                const Provider& provider) -> PathResult;

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

  // offset is the node-array slot under the search's NodeIndexSpace; only
  // the touched count survives for the expansion metric (audit 2026-07-11
  // M10).
  void touch_node(std::size_t offset) {
    generation_[offset] = epoch_;
    ++touched_count_;
  }

  std::vector<OpenNode> open_;
  std::vector<OpenNode> open_next_;
  // Parallel arrays deliberately: an interleaved {generation, g, state}
  // record was tried (audit 2026-07-11 M9) and measured 3-9% SLOWER --
  // partial-field visits (closed checks read generation+state only) waste
  // bandwidth on a 12-byte record, while the packed arrays keep 16
  // generations per cache line. See the optimization log, 2026-07-12.
  std::vector<std::uint32_t> generation_;
  std::uint32_t epoch_ = 1;
  std::vector<std::uint8_t> state_;
  std::vector<std::uint32_t> g_;
  std::vector<std::uint64_t> parent_;
  // Reached-node count only: unlike DistanceFieldScratch (whose touched
  // list feeds dependency capture), no A* consumer reads the indices, so
  // recording them cost an 8-byte store per reached node for nothing
  // (audit 2026-07-11 M10).
  std::size_t touched_count_ = 0;
  std::vector<Coord3> path_;
};

/// Owns reusable frontier, distance, dependency, and path storage for fields.
///
/// Returned paths borrow this object. Instances require external
/// synchronization; reserve node capacity for allocation-free warm queries.
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
    target_generation_.reserve(node_count);
    touched_.reserve(node_count);
    path_.reserve(node_count);
  }

  [[nodiscard]] auto capacity_nodes() const noexcept -> std::size_t {
    return distance_.capacity();
  }

 private:
  template <typename WorldType, typename Tag>
  friend auto build_distance_field(const WorldType& world, Coord3 goal,
                                   DistanceFieldScratch& scratch,
                                   MissingChunkPolicy policy)
      -> DistanceFieldResult;

  template <typename World, typename Class, typename Provider>
  friend auto build_weighted_distance_field_in_box(
      const World& world, Coord3 goal, Box3 domain,
      DistanceFieldScratch& scratch, MissingChunkPolicy policy,
      const Provider& provider) -> DistanceFieldResult;

  template <typename World, typename Tag>
  friend auto distance_field_path(const World& world, Coord3 start, Coord3 goal,
                                  DistanceFieldScratch& scratch) -> PathResult;

  // The weighted friends name the single-Class cores; the legacy tag-pair
  // overloads are thin forwarders and never touch scratch internals.
  template <typename WorldType, typename Class>
  friend auto build_weighted_distance_field(const WorldType& world, Coord3 goal,
                                            DistanceFieldScratch& scratch,
                                            MissingChunkPolicy policy)
      -> DistanceFieldResult;

  template <typename WorldType, typename Class, typename Provider>
  friend auto build_weighted_distance_field(const WorldType& world, Coord3 goal,
                                            DistanceFieldScratch& scratch,
                                            MissingChunkPolicy policy,
                                            const Provider& provider)
      -> DistanceFieldResult;

  template <typename World, typename Class>
  friend auto build_weighted_distance_field_in_box(
      const World& world, Coord3 goal, Box3 domain,
      DistanceFieldScratch& scratch, MissingChunkPolicy policy)
      -> DistanceFieldResult;

  template <typename World, typename Class, std::uint32_t MaxCost>
  friend auto detail::build_bounded_weighted_distance_field_core(
      const World& world, Coord3 goal, DistanceFieldScratch& scratch,
      MissingChunkPolicy policy, std::span<const std::uint64_t> settle_targets)
      -> DistanceFieldResult;

  // The public weighted_distance_field_path is a thin forwarder; the
  // private access lives in the detail core it forwards to.
  template <typename World, typename Class>
  friend auto detail::weighted_distance_field_path_core(
      const World& world, Coord3 start, Coord3 goal,
      DistanceFieldScratch& scratch, bool verify_residency) -> PathResult;

  template <typename World, typename Class, typename Provider>
  friend auto detail::weighted_distance_field_path_core(
      const World& world, Coord3 start, Coord3 goal,
      DistanceFieldScratch& scratch, bool verify_residency,
      const Provider& provider) -> PathResult;

  // The batch skips the core's per-member residency verification and
  // instead asserts the stamp once per group build.
  template <typename World, typename Class, std::uint32_t MaxCost>
  friend auto weighted_path_batch(const World& world,
                                  std::span<const PathRequest> requests,
                                  WeightedPathBatchScratch& scratch)
      -> std::span<const PathResult>;

  template <typename World, typename Class, std::uint32_t MaxCost,
            typename Provider>
  friend auto weighted_path_batch(const World& world,
                                  std::span<const PathRequest> requests,
                                  WeightedPathBatchScratch& scratch,
                                  const Provider& provider)
      -> std::span<const PathResult>;

  template <typename World, typename Tag>
  friend auto build_distance_field_product(const World& world,
                                           const GoalSet& goals,
                                           DistanceFieldScratch& scratch,
                                           DistanceFieldProduct& product)
      -> DistanceFieldResult;

  template <typename World, typename Tag, typename Provider>
  friend auto build_distance_field_product(const World& world,
                                           const GoalSet& goals,
                                           DistanceFieldScratch& scratch,
                                           DistanceFieldProduct& product,
                                           const Provider& provider)
      -> DistanceFieldResult;

  template <typename World, typename Tag>
  friend auto distance_field_product_path(const World& world, Coord3 start,
                                          const DistanceFieldProduct& product,
                                          DistanceFieldScratch& scratch)
      -> PathResult;

  template <typename World, typename Tag, typename Provider>
  friend auto distance_field_product_path(const World& world, Coord3 start,
                                          const DistanceFieldProduct& product,
                                          DistanceFieldScratch& scratch,
                                          const Provider& provider)
      -> PathResult;

  template <typename World, typename Tag>
  friend auto nearest_target(const World& world, Coord3 start,
                             const DistanceFieldProduct& product,
                             DistanceFieldScratch& scratch)
      -> NearestTargetResult;

  template <typename World, typename Tag, typename Provider>
  friend auto nearest_target(const World& world, Coord3 start,
                             const DistanceFieldProduct& product,
                             DistanceFieldScratch& scratch,
                             const Provider& provider) -> NearestTargetResult;

  template <typename World, typename Class, typename Provider>
  friend auto build_weighted_distance_field_product(
      const World& world, const GoalSet& goals, DistanceFieldScratch& scratch,
      DistanceFieldProduct& product, const Provider& provider)
      -> DistanceFieldResult;

  template <typename World, typename Class, typename Provider>
  friend auto weighted_distance_field_product_path(
      const World& world, Coord3 start, const DistanceFieldProduct& product,
      DistanceFieldScratch& scratch, const Provider& provider) -> PathResult;

  void clear_build() noexcept {
    advance_epoch();
    frontier_.clear();
    weighted_frontier_.clear();
    for (auto& bucket : weighted_buckets_) {
      bucket.clear();
    }
    touched_.clear();
    path_.clear();
    // These fields are the public-result validity sentinels. Per-node
    // distances and predecessors intentionally retain old bytes: the epoch
    // stamps invalidate them without an O(world-size) clearing pass.
    has_goal_ = false;
    model_class_identity_ = 0;
    model_provider_identity_ = 0;
    model_provider_revision_ = 0;
  }

  void clear_path() noexcept { path_.clear(); }

  void advance_epoch() noexcept {
    ++epoch_;
    if (epoch_ == 0) {
      std::fill(generation_.begin(), generation_.end(), 0);
      std::fill(target_generation_.begin(), target_generation_.end(), 0);
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

  void touch_node(std::size_t offset, std::uint64_t index) {
    generation_[offset] = epoch_;
    touched_.push_back(index);
  }

  // Dense-only convenience (offset == index) for the distance-field functions
  // not yet ported to NodeIndexSpace; all of those static_assert
  // AlwaysResident.
  void touch_node(std::uint64_t index) {
    touch_node(static_cast<std::size_t>(index), index);
  }

  // Settle-target marks share the build epoch: clear_build invalidates
  // them wholesale, and advance_epoch's wrap path re-zeros both stamp
  // arrays.
  void mark_settle_target(std::size_t offset) {
    target_generation_[offset] = epoch_;
  }

  [[nodiscard]] auto is_settle_target(std::size_t offset) const noexcept
      -> bool {
    return target_generation_[offset] == epoch_;
  }

  std::vector<std::uint64_t> frontier_;
  std::vector<PathScratch::OpenNode> weighted_frontier_;
  std::vector<std::vector<std::uint64_t>> weighted_buckets_;
  std::size_t weighted_bucket_capacity_ = 0;
  std::vector<std::uint32_t> generation_;
  std::uint32_t epoch_ = 1;
  std::vector<std::uint32_t> distance_;
  std::vector<std::uint32_t> target_generation_;
  std::vector<std::uint64_t> touched_;
  std::vector<Coord3> path_;
  // Per-chunk seen marks for build_distance_field_product's blocked-frontier
  // dependency pass; sized to the world's chunk count on use.
  std::vector<std::uint8_t> chunk_seen_;
  Coord3 goal_{};
  bool has_goal_ = false;
  std::uint64_t residency_fingerprint_ = 0;

  // Sparse residency staleness guard for the two-call build/read API. A built
  // distance field is indexed by resident-slot offset; if the resident set
  // changes between build_*distance_field and *distance_field_path (an
  // eviction/reload can rebind a slot to a different chunk), the reader would
  // descend a stale field and return a wrong path. build_* stamps the world's
  // residency fingerprint (a content hash of the resident set, not a per-world
  // counter -- so it also catches a scratch read against a different/copied/
  // swapped world, which a bare epoch could alias) and the readers reject a
  // mismatch (forcing a rebuild) instead of returning a wrong Found. Dense
  // worlds never evict, so both methods compile to a no-op / constant true and
  // keep dense byte-identical.
  template <typename World>
  void stamp_residency(const World& world) noexcept {
    if constexpr (!std::is_same_v<typename World::residency_type,
                                  AlwaysResident>) {
      residency_fingerprint_ = world.residency_fingerprint();
    }
  }
  template <typename World>
  [[nodiscard]] auto residency_matches(const World& world) const noexcept
      -> bool {
    if constexpr (std::is_same_v<typename World::residency_type,
                                 AlwaysResident>) {
      return true;
    } else {
      return residency_fingerprint_ == world.residency_fingerprint();
    }
  }

  template <typename Model>
  void stamp_model(const Model& model = Model{}) noexcept {
    model_class_identity_ = detail::tag_identity<typename Model::class_type>();
    model_lattice_identity_ =
        static_cast<std::uint32_t>(Model::lattice_identity);
    model_lattice_version_ = Model::lattice_version;
    model_step_identity_ =
        static_cast<std::uint32_t>(Model::step_policy_identity);
    model_cost_scale_ = Model::cost_scale;
    model_provider_identity_ =
        detail::tag_identity<typename Model::provider_type>();
    model_provider_revision_ = model.revision();
  }

  template <typename Model>
  [[nodiscard]] auto model_matches(const Model& model = Model{}) const noexcept
      -> bool {
    return model_class_identity_ ==
               detail::tag_identity<typename Model::class_type>() &&
           model_lattice_identity_ ==
               static_cast<std::uint32_t>(Model::lattice_identity) &&
           model_lattice_version_ == Model::lattice_version &&
           model_step_identity_ ==
               static_cast<std::uint32_t>(Model::step_policy_identity) &&
           model_cost_scale_ == Model::cost_scale &&
           model_provider_identity_ ==
               detail::tag_identity<typename Model::provider_type>() &&
           model_provider_revision_ == model.revision();
  }

  std::uintptr_t model_class_identity_ = 0;
  std::uint32_t model_lattice_identity_ = 0;
  std::uint32_t model_lattice_version_ = 0;
  std::uint32_t model_step_identity_ = 0;
  std::uint32_t model_cost_scale_ = 0;
  std::uintptr_t model_provider_identity_ = 0;
  std::uint64_t model_provider_revision_ = 0;
};

/// Owns all temporary and returned storage for weighted batch pathfinding.
///
/// Result spans and their paths remain valid until this object is mutated.
/// Reserve request, path, and search capacity to avoid warm allocations.
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
  template <typename World, typename Class, std::uint32_t MaxCost>
  friend auto weighted_path_batch(const World& world,
                                  std::span<const PathRequest> requests,
                                  WeightedPathBatchScratch& scratch)
      -> std::span<const PathResult>;

  template <typename World, typename Class, std::uint32_t MaxCost,
            typename Provider>
  friend auto weighted_path_batch(const World& world,
                                  std::span<const PathRequest> requests,
                                  WeightedPathBatchScratch& scratch,
                                  const Provider& provider)
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
  // Counting-sort member buckets (group_offsets_[g]..group_offsets_[g+1]
  // indexes group_members_), mirroring PathRequestRuntime's grouping, so
  // scattering a group's results touches only its own members instead of
  // rescanning every request per group (audit 2026-07-11 M1).
  std::vector<std::uint32_t> group_offsets_;
  std::vector<std::uint32_t> group_cursors_;
  std::vector<std::uint32_t> group_members_;
  // Per-group validated start tile indices handed to the field build as
  // settle targets (audit 2026-07-11 M3).
  std::vector<std::uint64_t> settle_targets_;
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

// The passability/cost leaves take a movement class OR a raw passable tag
// (normalized through movement_class_of, so every legacy <World, Tag> call
// site compiles unchanged and the identity class keeps codegen byte-identical
// to the raw field cast it replaces).
template <typename World, typename ClassOrTag>
[[nodiscard]] auto is_passable(const World& world, Coord3 coord) noexcept
    -> bool {
  using Class = movement::movement_class_of<ClassOrTag>;
  const auto resolved = world.try_resolve(coord);
  if (!resolved.has_value()) {
    return false;
  }
  if constexpr (std::is_same_v<typename World::residency_type,
                               SparseResident>) {
    const auto* page = world.try_chunk(resolved->chunk_key);
    return page != nullptr && Class::passable(*page, resolved->local_tile_id);
  } else {
    return Class::passable(world.chunk(resolved->chunk_key),
                           resolved->local_tile_id);
  }
}

template <typename World, typename ClassOrTag>
[[nodiscard]] auto is_passable_index(const World& world,
                                     std::uint64_t index) noexcept -> bool {
  using Class = movement::movement_class_of<ClassOrTag>;
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
    return Class::passable(*page, local_tile_id<Shape>(key));
  } else {
    return Class::passable(world.chunk(chunk_key<Shape>(key)),
                           local_tile_id<Shape>(key));
  }
}

// Unlike the passability leaves this REQUIRES a movement class: a raw tag
// would normalize to the unit-cost identity class and silently discard the
// cost field a legacy CostTag caller meant. Legacy <PassableTag, CostTag>
// entry points forward through movement::LegacyWeighted instead.
template <typename World, typename Class>
[[nodiscard]] auto tile_entry_cost_index(const World& world,
                                         std::uint64_t index) noexcept
    -> std::uint32_t {
  static_assert(std::derived_from<Class, movement::movement_class_tag>,
                "tile_entry_cost_index requires a MovementClass; wrap legacy "
                "tags in movement::LegacyWeighted<PassableTag, CostTag>.");
  using Shape = typename World::shape_type;
  using Storage = typename ShapeTraits<Shape>::TileKeyStorage;
  const auto key = TileKey<Shape>{static_cast<Storage>(index)};
  TESS_DIAG_EVENT(path_cost_read);
  return Class::entry_cost(world.chunk(chunk_key<Shape>(key)),
                           local_tile_id<Shape>(key));
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
// Keep this forced inline. Provider-aware readers made some Clang versions
// outline the helper even though every call is in a per-node reconstruction
// loop. That codegen cliff made the gated field-product replay and
// nearest-target workloads about 2.4x slower; inlining restores the original
// loop shape. MSVC needs its spelling while Clang/GCC share the GNU attribute.
#if defined(_MSC_VER)
__forceinline void
#elif defined(__GNUC__) || defined(__clang__)
__attribute__((always_inline)) inline void
#else
inline void
#endif
for_each_indexed_axis_neighbor(Coord3 coord, std::uint64_t index, Fn&& fn) {
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

/// Builds a dense-world weighted route and records replay dependencies.
///
/// The returned path borrows `product` and remains valid until its mutation.
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
  if (result.status == PathStatus::Found) {
    for (const auto coord : product.path_) {
      const auto key = tile_key<Shape>(coord);
      product.dependencies_.add_chunk(world, chunk_key<Shape>(key));
    }
  } else {
    // See detail::capture_failure_dependencies: a replayed failure must be
    // invalidated by any edit that could change the answer.
    detail::capture_failure_dependencies<Shape>(world, request, result.status,
                                                product.dependencies_);
  }

  return PathResult{product.status_, product.cost_, product.expanded_nodes_,
                    product.reached_nodes_, product.path_};
}

/// Replays a route product when all captured chunk versions still match.
template <typename World>
auto weighted_route_product_path(const World& world,
                                 const WeightedRouteProduct& product)
    -> PathResult {
  if (!product.is_valid(world)) {
    return PathResult{PathStatus::NoPath, 0, 0, 0, {}};
  }
  return PathResult{product.status_, product.cost_, 0, 0, product.path_};
}

/// Builds a dense weighted route by joining caller-provided portal waypoints.
///
/// The returned path borrows `product`; the function tolerates waypoint spans
/// that already refer to the product's own storage.
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

  std::vector<Coord3> stash;
  const auto source = product.stash_if_owned(waypoints, stash);

  product.clear();
  product.request_ = request;
  product.waypoints_.assign(source.begin(), source.end());

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
      // Same failure-dependency contract as build_weighted_route_product;
      // the failing segment's endpoints are the offending tiles.
      detail::capture_failure_dependencies<Shape>(
          world, segment_request, result.status, product.dependencies_);
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

/// Replays a portal-route product when its dependencies remain current.
template <typename World>
auto weighted_portal_route_product_path(
    const World& world, const WeightedPortalRouteProduct& product)
    -> PathResult {
  if (!product.is_valid(world)) {
    return PathResult{PathStatus::NoPath, 0, 0, 0, {}};
  }
  return PathResult{product.status_, product.cost_, 0, 0, product.path_};
}

/// Builds an unweighted goal-rooted field into caller-owned scratch.
template <typename WorldType, typename Tag>
auto build_distance_field(const WorldType& world, Coord3 goal,
                          DistanceFieldScratch& scratch,
                          [[maybe_unused]] MissingChunkPolicy policy)
    -> DistanceFieldResult {
  using Shape = typename WorldType::shape_type;
  using Space = detail::NodeIndexSpace<WorldType>;
  using Class = movement::movement_class_of<Tag>;
  using UnitClass = movement::detail::UnitMovementClass<Class>;
  using Model = ResolvedTransitionModel<WorldType, UnitClass>;
  constexpr auto infinite_distance = std::numeric_limits<std::uint32_t>::max();

  if constexpr (Model::cost_scale != 1) {
    return build_weighted_distance_field<WorldType, UnitClass>(world, goal,
                                                               scratch, policy);
  }

  TESS_DIAG_EVENT_VALUE(path_clear, scratch.touched_.size());
  scratch.clear_build();
  if (!contains<Shape>(goal)) {
    return DistanceFieldResult{PathStatus::InvalidGoal, 0, 0};
  }
  if constexpr (!Space::is_dense) {
    // A non-resident goal cannot seed the flood: indexing its node-array slot
    // would be out of bounds. Under Indeterminate the field is simply unknown.
    const Space residency{world};
    if (!residency.is_resident_index(detail::tile_index<Shape>(goal))) {
      return DistanceFieldResult{policy == MissingChunkPolicy::Indeterminate
                                     ? PathStatus::Indeterminate
                                     : PathStatus::InvalidGoal,
                                 0, 0};
    }
  }
  TESS_DIAG_EVENT(path_goal_passability_check);
  if (!detail::is_passable<WorldType, Tag>(world, goal)) {
    return DistanceFieldResult{PathStatus::InvalidGoal, 0, 0};
  }

  const Space space{world};
  const auto node_count = space.capacity_hint();
  if (scratch.distance_.size() != node_count) {
    TESS_DIAG_EVENT(path_initialize);
    scratch.generation_.assign(node_count, 0);
    scratch.distance_.assign(node_count, infinite_distance);
  }

  const auto goal_index = detail::tile_index<Shape>(goal);
  const auto goal_offset = space.offset(goal_index);
  scratch.goal_ = goal;
  scratch.has_goal_ = true;
  scratch.template stamp_model<Model>();
  scratch.stamp_residency(world);
  scratch.distance_[goal_offset] = 0;
  scratch.touch_node(goal_offset, goal_index);
  TESS_DIAG_EVENT(path_touch_node);
  scratch.frontier_.push_back(goal_index);
  TESS_DIAG_EVENT(path_heap_push);

  std::size_t expanded_nodes = 0;
  std::size_t head = 0;
  // Sparse: set when the flood skips a non-resident neighbor, so a field
  // truncated by a missing chunk can report Indeterminate under policy.
  [[maybe_unused]] bool crossed_missing = false;
  const auto model = Model{};
  while (head < scratch.frontier_.size()) {
    const auto current = scratch.frontier_[head];
    ++head;
    TESS_DIAG_EVENT(path_heap_pop);
    ++expanded_nodes;

    const auto current_offset = space.offset(current);
    const auto current_distance =
        scratch.distance_at(current_offset, infinite_distance);
    const auto current_coord = detail::tile_coord<Shape>(current);
    const auto visit_neighbor = [&](std::uint64_t neighbor_index) {
      if constexpr (!Space::is_dense) {
        // A non-resident neighbor has no node-array slot; remember the
        // boundary and skip it before computing an out-of-bounds offset.
        if (!space.is_resident_index(neighbor_index)) {
          crossed_missing = true;
          return;
        }
      }
      const auto neighbor_offset = space.offset(neighbor_index);
      if (scratch.is_current(neighbor_offset)) {
        TESS_DIAG_EVENT(path_neighbor_closed);
        return;
      }
      scratch.distance_[neighbor_offset] = current_distance + 1;
      scratch.touch_node(neighbor_offset, neighbor_index);
      TESS_DIAG_EVENT(path_touch_node);
      scratch.frontier_.push_back(neighbor_index);
      TESS_DIAG_EVENT(path_heap_push);
    };
    if constexpr (Model::preserves_default_connectivity &&
                  std::is_same_v<typename Model::step_policy,
                                 movement::DefaultSteps>) {
      detail::for_each_indexed_axis_neighbor<Shape>(
          current_coord, current, [&](Coord3, std::uint64_t neighbor_index) {
            TESS_DIAG_EVENT(path_neighbor_candidate);
            if constexpr (!Space::is_dense) {
              if (!space.is_resident_index(neighbor_index)) {
                crossed_missing = true;
                return;
              }
            }
            TESS_DIAG_EVENT(path_passability_check);
            if (!detail::is_passable_index<WorldType, Tag>(world,
                                                           neighbor_index)) {
              TESS_DIAG_EVENT(path_neighbor_blocked);
              return;
            }
            visit_neighbor(neighbor_index);
          });
    } else {
      model.for_each_reverse(world, current_coord, current, [&](auto probe) {
        TESS_DIAG_EVENT(path_neighbor_candidate);
        if (probe.availability == TransitionAvailability::MissingTopology) {
          crossed_missing = true;
          return;
        }
        visit_neighbor(probe.to_index);
      });
    }
  }

  if constexpr (!Space::is_dense) {
    if (crossed_missing && policy == MissingChunkPolicy::Indeterminate) {
      return DistanceFieldResult{PathStatus::Indeterminate, expanded_nodes,
                                 scratch.touched_.size()};
    }
  }
  return DistanceFieldResult{PathStatus::Found, expanded_nodes,
                             scratch.touched_.size()};
}

/// Reconstructs a path by descending the last matching distance-field build.
///
/// The returned path borrows `scratch`; stale sparse residency returns
/// `NoPath` so callers rebuild instead of reading mismatched node slots.
template <typename World, typename Tag>
auto distance_field_path(const World& world, Coord3 start, Coord3 goal,
                         DistanceFieldScratch& scratch) -> PathResult {
  using Shape = typename World::shape_type;
  using Space = detail::NodeIndexSpace<World>;
  using Class = movement::movement_class_of<Tag>;
  using UnitClass = movement::detail::UnitMovementClass<Class>;
  using Model = ResolvedTransitionModel<World, UnitClass>;
  constexpr auto infinite_distance = std::numeric_limits<std::uint32_t>::max();

  if constexpr (Model::cost_scale != 1) {
    return detail::weighted_distance_field_path_core<World, UnitClass>(
        world, start, goal, scratch, /*verify_residency=*/true);
  }

  scratch.clear_path();
  if (!contains<Shape>(start)) {
    return PathResult{PathStatus::InvalidStart, 0, 0, 0, scratch.path_};
  }
  if constexpr (!Space::is_dense) {
    // A non-resident start is not in the field; its node-array slot would be
    // out of bounds. Report InvalidStart -- the caller learns whether the
    // field itself was truncated from build_distance_field's status.
    const Space residency{world};
    if (!residency.is_resident_index(detail::tile_index<Shape>(start))) {
      return PathResult{PathStatus::InvalidStart, 0, 0, 0, scratch.path_};
    }
  }
  TESS_DIAG_EVENT(path_start_passability_check);
  if (!detail::is_passable<World, Tag>(world, start)) {
    return PathResult{PathStatus::InvalidStart, 0, 0, 0, scratch.path_};
  }
  if (!contains<Shape>(goal)) {
    return PathResult{PathStatus::InvalidGoal, 0, 0, 0, scratch.path_};
  }
  if (!scratch.has_goal_ || scratch.goal_ != goal ||
      !scratch.template model_matches<Model>() ||
      !scratch.residency_matches(world)) {
    return PathResult{PathStatus::NoPath, 0, 0, 0, scratch.path_};
  }

  const Space space{world};
  const auto start_index = detail::tile_index<Shape>(start);
  auto current = start_index;
  auto current_offset = space.offset(current);
  auto current_distance =
      scratch.distance_at(current_offset, infinite_distance);
  if (current_distance == infinite_distance) {
    return PathResult{PathStatus::NoPath, 0, 0, scratch.touched_.size(),
                      scratch.path_};
  }

  scratch.path_.push_back(start);
  TESS_DIAG_EVENT(path_reconstruct_node);
  const auto model = Model{};
  while (current_distance > 0) {
    const auto current_coord = detail::tile_coord<Shape>(current);
    auto next = current;
    auto next_distance = current_distance;
    const auto consider_neighbor = [&](std::uint64_t neighbor_index) {
      if constexpr (!Space::is_dense) {
        // A non-resident neighbor was never touched by the flood, so its
        // distance is infinite and it cannot be the descent step; skip it
        // before computing an out-of-bounds offset.
        if (!space.is_resident_index(neighbor_index)) {
          return;
        }
      }
      const auto neighbor_offset = space.offset(neighbor_index);
      const auto neighbor_distance =
          scratch.distance_at(neighbor_offset, infinite_distance);
      if (neighbor_distance < next_distance) {
        next = neighbor_index;
        next_distance = neighbor_distance;
      }
    };
    if constexpr (Model::preserves_default_connectivity &&
                  std::is_same_v<typename Model::step_policy,
                                 movement::DefaultSteps>) {
      detail::for_each_indexed_axis_neighbor<Shape>(
          current_coord, current, [&](Coord3, std::uint64_t neighbor_index) {
            consider_neighbor(neighbor_index);
          });
    } else {
      model.for_each_forward(world, current_coord, current, [&](auto probe) {
        if (probe.availability == TransitionAvailability::Legal) {
          consider_neighbor(probe.to_index);
        }
      });
    }

    if (next == current || next_distance + 1 != current_distance) {
      scratch.path_.clear();
      return PathResult{PathStatus::NoPath, 0, 0, scratch.touched_.size(),
                        scratch.path_};
    }

    current = next;
    current_offset = space.offset(current);
    current_distance = scratch.distance_at(current_offset, infinite_distance);
    scratch.path_.push_back(detail::tile_coord<Shape>(current));
    TESS_DIAG_EVENT(path_reconstruct_node);
  }

  return PathResult{
      PathStatus::Found, scratch.distance_[space.offset(start_index)],
      scratch.path_.size(), scratch.touched_.size(), scratch.path_};
}

/// Builds a provider-aware unweighted field through reverse Dijkstra.
template <typename WorldType, typename Tag, typename Provider>
auto build_distance_field(const WorldType& world, Coord3 goal,
                          DistanceFieldScratch& scratch,
                          MissingChunkPolicy policy, const Provider& provider)
    -> DistanceFieldResult {
  using Class = movement::movement_class_of<Tag>;
  using UnitClass = movement::detail::UnitMovementClass<Class>;
  return build_weighted_distance_field<WorldType, UnitClass, Provider>(
      world, goal, scratch, policy, provider);
}

/// Reads a provider-aware unweighted field built with the same provider.
template <typename World, typename Tag, typename Provider>
auto distance_field_path(const World& world, Coord3 start, Coord3 goal,
                         DistanceFieldScratch& scratch,
                         const Provider& provider) -> PathResult {
  using Class = movement::movement_class_of<Tag>;
  using UnitClass = movement::detail::UnitMovementClass<Class>;
  return detail::weighted_distance_field_path_core<World, UnitClass, Provider>(
      world, start, goal, scratch, /*verify_residency=*/true, provider);
}

/// Builds a movement-class weighted field into reusable caller scratch.
template <typename WorldType, typename Class, typename Provider>
auto build_weighted_distance_field(const WorldType& world, Coord3 goal,
                                   DistanceFieldScratch& scratch,
                                   [[maybe_unused]] MissingChunkPolicy policy,
                                   const Provider& provider)
    -> DistanceFieldResult {
  static_assert(std::derived_from<Class, movement::movement_class_tag>,
                "build_weighted_distance_field<World, Class> requires a "
                "MovementClass; legacy tag pairs go through the "
                "<World, PassableTag, CostTag> overload.");
  using Shape = typename WorldType::shape_type;
  using Space = detail::NodeIndexSpace<WorldType>;
  using Model = ResolvedTransitionModel<WorldType, Class, Provider>;
  constexpr auto infinite_distance = std::numeric_limits<std::uint32_t>::max();

  TESS_DIAG_EVENT_VALUE(path_clear, scratch.touched_.size());
  scratch.clear_build();
  if (!contains<Shape>(goal)) {
    return DistanceFieldResult{PathStatus::InvalidGoal, 0, 0};
  }
  if constexpr (!Space::is_dense) {
    // A non-resident goal cannot seed the flood; under Indeterminate the field
    // is simply unknown. Resolve it before is_passable/entry-cost read the
    // goal chunk.
    const Space residency{world};
    if (!residency.is_resident_index(detail::tile_index<Shape>(goal))) {
      return DistanceFieldResult{policy == MissingChunkPolicy::Indeterminate
                                     ? PathStatus::Indeterminate
                                     : PathStatus::InvalidGoal,
                                 0, 0};
    }
  }
  TESS_DIAG_EVENT(path_goal_passability_check);
  if (!detail::is_passable<WorldType, Class>(world, goal)) {
    return DistanceFieldResult{PathStatus::InvalidGoal, 0, 0};
  }

  const auto goal_index = detail::tile_index<Shape>(goal);
  if (detail::tile_entry_cost_index<WorldType, Class>(world, goal_index) == 0) {
    return DistanceFieldResult{PathStatus::InvalidGoal, 0, 0};
  }

  const Space space{world};
  const auto node_count = space.capacity_hint();
  if (scratch.distance_.size() != node_count) {
    TESS_DIAG_EVENT(path_initialize);
    scratch.generation_.assign(node_count, 0);
    scratch.distance_.assign(node_count, infinite_distance);
  }

  const auto goal_offset = space.offset(goal_index);
  const auto model = Model{provider};
  scratch.goal_ = goal;
  scratch.has_goal_ = true;
  scratch.template stamp_model<Model>(model);
  scratch.stamp_residency(world);
  scratch.distance_[goal_offset] = 0;
  scratch.touch_node(goal_offset, goal_index);
  TESS_DIAG_EVENT(path_touch_node);
  scratch.weighted_frontier_.push_back(PathScratch::OpenNode{goal_index, 0, 0});
  std::push_heap(scratch.weighted_frontier_.begin(),
                 scratch.weighted_frontier_.end(), detail::open_node_less);
  TESS_DIAG_EVENT(path_heap_push);

  std::size_t expanded_nodes = 0;
  [[maybe_unused]] bool crossed_missing = false;
  auto cost_overflow = false;
  while (!scratch.weighted_frontier_.empty()) {
    TESS_DIAG_EVENT(path_heap_pop);
    std::pop_heap(scratch.weighted_frontier_.begin(),
                  scratch.weighted_frontier_.end(), detail::open_node_less);
    const auto current = scratch.weighted_frontier_.back();
    scratch.weighted_frontier_.pop_back();

    const auto current_offset = space.offset(current.index);
    const auto current_distance =
        scratch.distance_at(current_offset, infinite_distance);
    if (current.g != current_distance) {
      TESS_DIAG_EVENT_VALUE(path_skip_pop, false);
      continue;
    }
    ++expanded_nodes;

    const auto current_coord = detail::tile_coord<Shape>(current.index);
    model.for_each_reverse(
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
          const auto neighbor_index = probe.to_index;
          if constexpr (!Space::is_dense) {
            if (!space.is_resident_index(neighbor_index)) {
              crossed_missing = true;
              return;
            }
          }
          const auto neighbor_offset = space.offset(neighbor_index);
          TESS_DIAG_EVENT(path_relax_attempt);
          if (!scratch.is_current(neighbor_offset)) {
            scratch.distance_[neighbor_offset] = infinite_distance;
            scratch.touch_node(neighbor_offset, neighbor_index);
            TESS_DIAG_EVENT(path_touch_node);
          }

          const auto next_distance =
              detail::saturating_add(current_distance, probe.cost);
          if (next_distance == infinite_distance) {
            cost_overflow = true;
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

  if constexpr (!Space::is_dense) {
    if (crossed_missing && policy == MissingChunkPolicy::Indeterminate) {
      return DistanceFieldResult{PathStatus::Indeterminate, expanded_nodes,
                                 scratch.touched_.size()};
    }
  }
  return DistanceFieldResult{
      cost_overflow ? PathStatus::CostOverflow : PathStatus::Found,
      expanded_nodes, scratch.touched_.size()};
}

template <typename WorldType, typename Class>
auto build_weighted_distance_field(const WorldType& world, Coord3 goal,
                                   DistanceFieldScratch& scratch,
                                   MissingChunkPolicy policy)
    -> DistanceFieldResult {
  return build_weighted_distance_field<WorldType, Class, AdjacentTransitions>(
      world, goal, scratch, policy, AdjacentTransitions{});
}

template <typename World, typename PassableTag, typename CostTag>
auto build_weighted_distance_field(const World& world, Coord3 goal,
                                   DistanceFieldScratch& scratch,
                                   MissingChunkPolicy policy)
    -> DistanceFieldResult {
  return build_weighted_distance_field<
      World, movement::LegacyWeighted<PassableTag, CostTag>>(world, goal,
                                                             scratch, policy);
}

#include <tess/path/detail/weighted_batch.h>

}  // namespace tess

#include <tess/path/route_cache.h>
