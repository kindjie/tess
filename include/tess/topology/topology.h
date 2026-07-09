#pragma once

#include <tess/core/shape.h>
#include <tess/storage/residency.h>
#include <tess/storage/world.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <type_traits>
#include <vector>

namespace tess {

// RegionGraph is a class template on the world residency policy so a sparse
// graph is a distinct type from a dense one (no silent dense indexing). The
// AlwaysResident alias below keeps every existing dense call site unchanged.
template <typename Residency>
class RegionGraphT;

// Local region ids are 1-based: 0 is the invalid sentinel
// (`invalid_local_region`). A valid id maps to
// `LocalChunkTopology::regions()[value - 1]`; use
// `LocalChunkTopology::region(id)` for checked access.
struct LocalRegionId {
  std::uint32_t value = 0;

  friend constexpr bool operator==(LocalRegionId lhs,
                                   LocalRegionId rhs) noexcept = default;
};

inline constexpr LocalRegionId invalid_local_region{};

// Sentinel returned by RegionGraph::region_index for invalid or
// out-of-range region references.
inline constexpr std::uint32_t invalid_region_index =
    std::numeric_limits<std::uint32_t>::max();

enum class BoundaryFace : std::uint8_t {
  NegativeX,
  PositiveX,
  NegativeY,
  PositiveY,
  NegativeZ,
  PositiveZ,
};

enum class TopologyStatus : std::uint8_t {
  Built,
  InvalidChunk,
};

struct LocalRegion {
  LocalRegionId id{};
  std::size_t tile_count = 0;
  Box3 bounds{};
  std::size_t boundary_exit_count = 0;

  friend constexpr bool operator==(const LocalRegion& lhs,
                                   const LocalRegion& rhs) noexcept = default;
};

struct LocalBoundaryExit {
  LocalRegionId region{};
  LocalTileId local_tile{};
  Coord3 coord{};
  BoundaryFace face = BoundaryFace::NegativeX;
  ChunkKey target_chunk{};

  friend constexpr bool operator==(const LocalBoundaryExit& lhs,
                                   const LocalBoundaryExit& rhs) noexcept =
      default;
};

struct LocalTopologyResult {
  TopologyStatus status = TopologyStatus::Built;
  std::size_t region_count = 0;
  std::size_t passable_tile_count = 0;
  std::size_t boundary_exit_count = 0;
  std::uint32_t version = 0;
};

struct RegionRef {
  ChunkKey chunk{};
  LocalRegionId region{};

  friend constexpr bool operator==(RegionRef lhs,
                                   RegionRef rhs) noexcept = default;
};

struct RegionPortal {
  RegionRef from{};
  RegionRef to{};
  Coord3 from_coord{};
  Coord3 to_coord{};
  BoundaryFace face = BoundaryFace::NegativeX;

  friend constexpr bool operator==(const RegionPortal& lhs,
                                   const RegionPortal& rhs) noexcept = default;
};

enum class ReachabilityStatus : std::uint8_t {
  Reachable,
  Unreachable,
  InvalidStart,
  InvalidGoal,
  // The query reached the edge of the resident set: a region on the searched
  // side has a boundary exit into a non-resident chunk, so a route through the
  // unloaded region cannot be ruled out. Distinct from Unreachable, which means
  // a route was definitively searched and none exists within the resident set.
  // Only ever returned for sparse worlds. Appended last so existing enumerator
  // values do not shift.
  Indeterminate,
};

struct ReachabilityResult {
  ReachabilityStatus status = ReachabilityStatus::Unreachable;
  std::size_t visited_regions = 0;
};

class LocalTopologyScratch {
 public:
  void reserve_tiles(std::size_t count) { stack_.reserve(count); }

  [[nodiscard]] auto capacity() const noexcept -> std::size_t {
    return stack_.capacity();
  }

 private:
  template <typename World, typename PassableTag>
  friend auto build_local_chunk_topology(const World& world, ChunkKey chunk,
                                         LocalTopologyScratch& scratch,
                                         class LocalChunkTopology& topology)
      -> LocalTopologyResult;

  std::vector<LocalTileId> stack_;
};

class RegionGraphScratch {
 public:
  void reserve_regions(std::size_t count) {
    frontier_.reserve(count);
    visited_epoch_.reserve(count);
  }

  [[nodiscard]] auto capacity() const noexcept -> std::size_t {
    return frontier_.capacity();
  }

 private:
  template <typename Shape, typename Residency>
  friend auto reachable(const RegionGraphT<Residency>& graph, Coord3 start,
                        Coord3 goal, RegionGraphScratch& scratch)
      -> ReachabilityResult;

  // Epoch-stamped visited marks: a region index is visited when its
  // generation stamp matches the current epoch, so traversals reset in
  // O(1) instead of clearing the whole vector.
  void begin_traversal(std::size_t region_count) {
    frontier_.clear();
    if (visited_epoch_.size() < region_count) {
      visited_epoch_.resize(region_count, 0);
    }
    ++epoch_;
    if (epoch_ == 0) {
      std::fill(visited_epoch_.begin(), visited_epoch_.end(), 0);
      epoch_ = 1;
    }
  }

  [[nodiscard]] auto is_visited(std::uint32_t region_index) const noexcept
      -> bool {
    return visited_epoch_[static_cast<std::size_t>(region_index)] == epoch_;
  }

  void visit(std::uint32_t region_index) noexcept {
    visited_epoch_[static_cast<std::size_t>(region_index)] = epoch_;
  }

  std::vector<std::uint32_t> frontier_;
  std::vector<std::uint32_t> visited_epoch_;
  std::uint32_t epoch_ = 0;
};

class LocalChunkTopology {
 public:
  void clear() noexcept {
    chunk_ = ChunkKey{0};
    chunk_coord_ = ChunkCoord3{};
    version_ = 0;
    region_ids_.clear();
    regions_.clear();
    boundary_exits_.clear();
  }

  [[nodiscard]] auto chunk() const noexcept -> ChunkKey { return chunk_; }

  [[nodiscard]] auto chunk_coord() const noexcept -> ChunkCoord3 {
    return chunk_coord_;
  }

  [[nodiscard]] auto version() const noexcept -> std::uint32_t {
    return version_;
  }

  [[nodiscard]] auto region_ids() const noexcept
      -> std::span<const LocalRegionId> {
    return {region_ids_.data(), region_ids_.size()};
  }

  [[nodiscard]] auto regions() const noexcept -> std::span<const LocalRegion> {
    return {regions_.data(), regions_.size()};
  }

  // Checked accessor for the 1-based LocalRegionId convention: id N maps to
  // regions()[N - 1]. Returns nullptr for the invalid sentinel and for
  // out-of-range ids.
  [[nodiscard]] auto region(LocalRegionId id) const noexcept
      -> const LocalRegion* {
    if (id.value == 0 || id.value > regions_.size()) {
      return nullptr;
    }
    return &regions_[static_cast<std::size_t>(id.value) - 1];
  }

  [[nodiscard]] auto boundary_exits() const noexcept
      -> std::span<const LocalBoundaryExit> {
    return {boundary_exits_.data(), boundary_exits_.size()};
  }

  [[nodiscard]] auto region_at(LocalTileId tile) const noexcept
      -> LocalRegionId {
    if (tile.value >= region_ids_.size()) {
      return invalid_local_region;
    }
    return region_ids_[static_cast<std::size_t>(tile.value)];
  }

  template <typename Shape>
  [[nodiscard]] auto region_at(LocalCoord3 coord) const noexcept
      -> LocalRegionId {
    return region_at(local_tile_id<Shape>(coord));
  }

 private:
  template <typename World, typename PassableTag>
  friend auto build_local_chunk_topology(const World& world, ChunkKey chunk,
                                         LocalTopologyScratch& scratch,
                                         LocalChunkTopology& topology)
      -> LocalTopologyResult;

  ChunkKey chunk_{};
  ChunkCoord3 chunk_coord_{};
  std::uint32_t version_ = 0;
  std::vector<LocalRegionId> region_ids_;
  std::vector<LocalRegion> regions_;
  std::vector<LocalBoundaryExit> boundary_exits_;
};

namespace detail {

// Sparse-only companion state for RegionGraphT. Empty (via the explicit
// AlwaysResident specialization) so a dense graph carries zero extra storage
// through the [[no_unique_address]] member.
template <typename Residency>
struct RegionGraphSparseData {
  // Frozen at build, sorted ascending by ChunkKey.value: the resident chunk set
  // this graph was built over. Resolves ChunkKey -> local index by lower_bound,
  // world-free, so eviction after the build cannot invalidate the graph.
  std::vector<ChunkKey> topology_keys_;
  // One flag per global region: the region has a boundary exit into a chunk
  // that was non-resident at build time (a route through it cannot be ruled
  // out).
  std::vector<std::uint8_t> region_reaches_missing_;
  // Per local topology: the residency generation at build, for staleness
  // detection in update_region_graph.
  std::vector<std::uint64_t> frozen_generations_;
};

template <>
struct RegionGraphSparseData<AlwaysResident> {};

}  // namespace detail

template <typename Residency>
class RegionGraphT {
 public:
  void clear() noexcept {
    local_topologies_.clear();
    portals_.clear();
    region_offsets_.clear();
    adjacency_starts_.clear();
    adjacency_targets_.clear();
    if constexpr (!std::is_same_v<Residency, AlwaysResident>) {
      sparse_.topology_keys_.clear();
      sparse_.region_reaches_missing_.clear();
      sparse_.frozen_generations_.clear();
    }
  }

  [[nodiscard]] auto local_topologies() const noexcept
      -> std::span<const LocalChunkTopology> {
    return {local_topologies_.data(), local_topologies_.size()};
  }

  [[nodiscard]] auto portals() const noexcept -> std::span<const RegionPortal> {
    return {portals_.data(), portals_.size()};
  }

  [[nodiscard]] auto local_topology(ChunkKey chunk) const noexcept
      -> const LocalChunkTopology* {
    if constexpr (std::is_same_v<Residency, AlwaysResident>) {
      if (chunk.value >= local_topologies_.size()) {
        return nullptr;
      }
      return &local_topologies_[static_cast<std::size_t>(chunk.value)];
    } else {
      const auto idx = local_index(chunk);
      if (idx == npos) {
        return nullptr;
      }
      return &local_topologies_[idx];
    }
  }

  template <typename Shape>
  [[nodiscard]] auto region_of(Coord3 coord) const noexcept -> RegionRef {
    if (!contains<Shape>(coord)) {
      return RegionRef{ChunkKey{std::numeric_limits<std::uint64_t>::max()},
                       invalid_local_region};
    }
    const auto key = chunk_key<Shape>(chunk_coord<Shape>(coord));
    const auto* local = local_topology(key);
    if (local == nullptr) {
      return RegionRef{key, invalid_local_region};
    }
    return RegionRef{
        key, local->region_at(local_tile_id<Shape>(local_coord<Shape>(coord)))};
  }

  // Total region count across all chunks in the dense global region index.
  [[nodiscard]] auto region_count() const noexcept -> std::uint32_t {
    return region_offsets_.empty() ? 0U : region_offsets_.back();
  }

  // Maps a region reference to its dense global index:
  // region_offsets_[chunk] + (1-based local id - 1). Returns
  // invalid_region_index for invalid or out-of-range references.
  [[nodiscard]] auto region_index(RegionRef ref) const noexcept
      -> std::uint32_t {
    if constexpr (std::is_same_v<Residency, AlwaysResident>) {
      if (ref.region == invalid_local_region ||
          ref.chunk.value + 1 >= region_offsets_.size()) {
        return invalid_region_index;
      }
      const auto chunk = static_cast<std::size_t>(ref.chunk.value);
      const auto index = region_offsets_[chunk] + ref.region.value - 1;
      if (index >= region_offsets_[chunk + 1]) {
        return invalid_region_index;
      }
      return index;
    } else {
      if (ref.region == invalid_local_region) {
        return invalid_region_index;
      }
      const auto li = local_index(ref.chunk);
      if (li == npos || li + 1 >= region_offsets_.size()) {
        return invalid_region_index;
      }
      const auto index = region_offsets_[li] + ref.region.value - 1;
      if (index >= region_offsets_[li + 1]) {
        return invalid_region_index;
      }
      return index;
    }
  }

 private:
  template <typename World, typename PassableTag>
  friend auto build_region_graph(
      const World& world, LocalTopologyScratch& scratch,
      RegionGraphT<typename World::residency_type>& graph)
      -> LocalTopologyResult;

  template <typename World, typename PassableTag>
  friend auto update_region_graph(
      const World& world, LocalTopologyScratch& scratch,
      RegionGraphT<typename World::residency_type>& graph,
      std::span<const ChunkKey> dirty_chunks) -> LocalTopologyResult;

  template <typename Shape, typename OtherResidency>
  friend auto reachable(const RegionGraphT<OtherResidency>& graph, Coord3 start,
                        Coord3 goal, RegionGraphScratch& scratch)
      -> ReachabilityResult;

  template <typename OtherWorld>
  friend auto is_region_graph_fresh(
      const OtherWorld& world,
      const RegionGraphT<typename OtherWorld::residency_type>& graph) noexcept
      -> bool;

  // Rebuilds the dense region index and the CSR portal adjacency. The CSR
  // fill preserves portal order within each from-region bucket so
  // traversal remains deterministic.
  void rebuild_region_index() {
    region_offsets_.assign(local_topologies_.size() + 1, 0);
    for (std::size_t i = 0; i < local_topologies_.size(); ++i) {
      region_offsets_[i + 1] =
          region_offsets_[i] +
          static_cast<std::uint32_t>(local_topologies_[i].regions().size());
    }

    adjacency_starts_.assign(static_cast<std::size_t>(region_count()) + 1, 0);
    for (const auto& portal : portals_) {
      ++adjacency_starts_[static_cast<std::size_t>(region_index(portal.from)) +
                          1];
    }
    for (std::size_t i = 1; i < adjacency_starts_.size(); ++i) {
      adjacency_starts_[i] += adjacency_starts_[i - 1];
    }

    adjacency_targets_.resize(portals_.size());
    auto cursor = adjacency_starts_;
    for (const auto& portal : portals_) {
      const auto from = static_cast<std::size_t>(region_index(portal.from));
      adjacency_targets_[static_cast<std::size_t>(cursor[from]++)] =
          region_index(portal.to);
    }

    if constexpr (!std::is_same_v<Residency, AlwaysResident>) {
      // Flag every region with a boundary exit into a non-resident chunk. The
      // membership test (has_chunk) -- not portal absence -- is what separates
      // "unknown, unloaded" from "a real wall in a resident neighbor". Keyed by
      // the same global region index the BFS/CSR use, so reachable reads it
      // directly.
      sparse_.region_reaches_missing_.assign(
          static_cast<std::size_t>(region_count()), 0);
      for (const auto& topology : local_topologies_) {
        for (const auto& exit : topology.boundary_exits()) {
          if (has_chunk(exit.target_chunk)) {
            continue;
          }
          const auto idx =
              region_index(RegionRef{topology.chunk(), exit.region});
          if (idx != invalid_region_index) {
            sparse_.region_reaches_missing_[static_cast<std::size_t>(idx)] = 1;
          }
        }
      }
    }
  }

  static constexpr std::size_t npos = static_cast<std::size_t>(-1);

  // Sparse only: resolve a ChunkKey to its position in the frozen sorted key
  // set, or npos if the chunk was not resident when this graph was built.
  [[nodiscard]] auto local_index(ChunkKey chunk) const noexcept -> std::size_t {
    const auto& keys = sparse_.topology_keys_;
    const auto it = std::lower_bound(
        keys.begin(), keys.end(), chunk,
        [](ChunkKey lhs, ChunkKey rhs) { return lhs.value < rhs.value; });
    if (it == keys.end() || it->value != chunk.value) {
      return npos;
    }
    return static_cast<std::size_t>(it - keys.begin());
  }

  [[nodiscard]] auto has_chunk(ChunkKey chunk) const noexcept -> bool {
    return local_index(chunk) != npos;
  }

  std::vector<LocalChunkTopology> local_topologies_;
  std::vector<RegionPortal> portals_;
  std::vector<std::uint32_t> region_offsets_;
  std::vector<std::uint32_t> adjacency_starts_;
  std::vector<std::uint32_t> adjacency_targets_;
  [[no_unique_address]] detail::RegionGraphSparseData<Residency> sparse_;
};

using RegionGraph = RegionGraphT<AlwaysResident>;
using SparseRegionGraph = RegionGraphT<SparseResident>;

namespace detail {

template <typename Shape>
[[nodiscard]] constexpr auto local_tile_coord(LocalTileId id) noexcept
    -> LocalCoord3 {
  const auto chunk = ShapeTraits<Shape>::chunk;
  const auto xy = chunk.x * chunk.y;
  const auto z = id.value / xy;
  const auto remainder = id.value % xy;
  return LocalCoord3{
      remainder % chunk.x,
      remainder / chunk.x,
      z,
  };
}

template <typename Shape>
constexpr void add_boundary_exit(std::vector<LocalBoundaryExit>& exits,
                                 LocalRegion& region, LocalTileId local_tile,
                                 Coord3 coord, BoundaryFace face,
                                 ChunkCoord3 target_chunk) {
  exits.push_back(LocalBoundaryExit{
      region.id,
      local_tile,
      coord,
      face,
      chunk_key<Shape>(target_chunk),
  });
  ++region.boundary_exit_count;
}

template <typename Shape>
constexpr void add_boundary_exits(std::vector<LocalBoundaryExit>& exits,
                                  ChunkCoord3 chunk_coord, LocalRegion& region,
                                  LocalTileId local_tile, LocalCoord3 local,
                                  Coord3 coord) {
  const auto chunk = ShapeTraits<Shape>::chunk;

  if (local.x == 0 && chunk_coord.x > 0) {
    auto target = chunk_coord;
    --target.x;
    add_boundary_exit<Shape>(exits, region, local_tile, coord,
                             BoundaryFace::NegativeX, target);
  }
  if (local.x + 1 == chunk.x &&
      chunk_coord.x + 1 < ShapeTraits<Shape>::chunk_count_x) {
    auto target = chunk_coord;
    ++target.x;
    add_boundary_exit<Shape>(exits, region, local_tile, coord,
                             BoundaryFace::PositiveX, target);
  }
  if (local.y == 0 && chunk_coord.y > 0) {
    auto target = chunk_coord;
    --target.y;
    add_boundary_exit<Shape>(exits, region, local_tile, coord,
                             BoundaryFace::NegativeY, target);
  }
  if (local.y + 1 == chunk.y &&
      chunk_coord.y + 1 < ShapeTraits<Shape>::chunk_count_y) {
    auto target = chunk_coord;
    ++target.y;
    add_boundary_exit<Shape>(exits, region, local_tile, coord,
                             BoundaryFace::PositiveY, target);
  }
  if (local.z == 0 && chunk_coord.z > 0) {
    auto target = chunk_coord;
    --target.z;
    add_boundary_exit<Shape>(exits, region, local_tile, coord,
                             BoundaryFace::NegativeZ, target);
  }
  if (local.z + 1 == chunk.z &&
      chunk_coord.z + 1 < ShapeTraits<Shape>::chunk_count_z) {
    auto target = chunk_coord;
    ++target.z;
    add_boundary_exit<Shape>(exits, region, local_tile, coord,
                             BoundaryFace::PositiveZ, target);
  }
}

template <typename Shape, typename Fn>
constexpr void for_each_local_axis_neighbor(LocalCoord3 coord, Fn&& fn) {
  const auto chunk = ShapeTraits<Shape>::chunk;
  if (coord.x + 1 < chunk.x) {
    fn(LocalCoord3{coord.x + 1, coord.y, coord.z});
  }
  if (coord.x > 0) {
    fn(LocalCoord3{coord.x - 1, coord.y, coord.z});
  }
  if (coord.y + 1 < chunk.y) {
    fn(LocalCoord3{coord.x, coord.y + 1, coord.z});
  }
  if (coord.y > 0) {
    fn(LocalCoord3{coord.x, coord.y - 1, coord.z});
  }
  if (coord.z + 1 < chunk.z) {
    fn(LocalCoord3{coord.x, coord.y, coord.z + 1});
  }
  if (coord.z > 0) {
    fn(LocalCoord3{coord.x, coord.y, coord.z - 1});
  }
}

constexpr void include_coord_in_bounds(LocalRegion& region,
                                       Coord3 coord) noexcept {
  if (region.tile_count == 0) {
    region.bounds = Box3{coord, Extent3{1, 1, 1}};
    return;
  }

  const auto end = [](std::int64_t origin, std::uint64_t extent) {
    return origin + static_cast<std::int64_t>(extent);
  };
  const auto min = [](std::int64_t lhs, std::int64_t rhs) {
    return lhs < rhs ? lhs : rhs;
  };
  const auto max = [](std::int64_t lhs, std::int64_t rhs) {
    return lhs < rhs ? rhs : lhs;
  };
  const auto min_x = min(region.bounds.origin.x, coord.x);
  const auto min_y = min(region.bounds.origin.y, coord.y);
  const auto min_z = min(region.bounds.origin.z, coord.z);
  const auto max_x =
      max(end(region.bounds.origin.x, region.bounds.extent.x), coord.x + 1);
  const auto max_y =
      max(end(region.bounds.origin.y, region.bounds.extent.y), coord.y + 1);
  const auto max_z =
      max(end(region.bounds.origin.z, region.bounds.extent.z), coord.z + 1);

  region.bounds = Box3{
      Coord3{min_x, min_y, min_z},
      Extent3{
          static_cast<std::uint64_t>(max_x - min_x),
          static_cast<std::uint64_t>(max_y - min_y),
          static_cast<std::uint64_t>(max_z - min_z),
      },
  };
}

[[nodiscard]] constexpr auto neighbor_coord(Coord3 coord,
                                            BoundaryFace face) noexcept
    -> Coord3 {
  switch (face) {
    case BoundaryFace::NegativeX:
      return Coord3{coord.x - 1, coord.y, coord.z};
    case BoundaryFace::PositiveX:
      return Coord3{coord.x + 1, coord.y, coord.z};
    case BoundaryFace::NegativeY:
      return Coord3{coord.x, coord.y - 1, coord.z};
    case BoundaryFace::PositiveY:
      return Coord3{coord.x, coord.y + 1, coord.z};
    case BoundaryFace::NegativeZ:
      return Coord3{coord.x, coord.y, coord.z - 1};
    case BoundaryFace::PositiveZ:
      return Coord3{coord.x, coord.y, coord.z + 1};
  }
  return coord;
}

template <typename Shape, typename Fn>
constexpr void for_each_face_neighbor_chunk(ChunkCoord3 coord, Fn&& fn) {
  using Traits = ShapeTraits<Shape>;
  if (coord.x > 0) {
    auto target = coord;
    --target.x;
    fn(target);
  }
  if (coord.x + 1 < Traits::chunk_count_x) {
    auto target = coord;
    ++target.x;
    fn(target);
  }
  if (coord.y > 0) {
    auto target = coord;
    --target.y;
    fn(target);
  }
  if (coord.y + 1 < Traits::chunk_count_y) {
    auto target = coord;
    ++target.y;
    fn(target);
  }
  if (coord.z > 0) {
    auto target = coord;
    --target.z;
    fn(target);
  }
  if (coord.z + 1 < Traits::chunk_count_z) {
    auto target = coord;
    ++target.z;
    fn(target);
  }
}

// Derives directed portals for every boundary exit of one chunk topology,
// in exit order, appending only exits whose neighbor tile maps to a
// passable region.
template <typename Shape, typename Residency>
void append_chunk_portals(const RegionGraphT<Residency>& graph,
                          const LocalChunkTopology& topology,
                          std::vector<RegionPortal>& portals) {
  for (const auto& exit : topology.boundary_exits()) {
    const auto to_coord = neighbor_coord(exit.coord, exit.face);
    const auto target = graph.template region_of<Shape>(to_coord);
    if (target.region == invalid_local_region) {
      continue;
    }
    portals.push_back(RegionPortal{
        RegionRef{topology.chunk(), exit.region},
        target,
        exit.coord,
        to_coord,
        exit.face,
    });
  }
}

}  // namespace detail

template <typename World, typename PassableTag>
auto build_local_chunk_topology(const World& world, ChunkKey chunk,
                                LocalTopologyScratch& scratch,
                                LocalChunkTopology& topology)
    -> LocalTopologyResult {
  using Shape = typename World::shape_type;
  using Traits = ShapeTraits<Shape>;

  topology.clear();
  if (chunk.value >= Traits::chunk_count) {
    return LocalTopologyResult{TopologyStatus::InvalidChunk, 0, 0, 0, 0};
  }

  topology.chunk_ = chunk;
  topology.chunk_coord_ = chunk_coord<Shape>(chunk);
  topology.version_ = world.meta(chunk).topology_version;
  topology.region_ids_.assign(
      static_cast<std::size_t>(Traits::local_tile_count), invalid_local_region);
  scratch.stack_.clear();

  const auto passable = world.template field_span<PassableTag>(chunk);
  std::size_t passable_tiles = 0;

  for (std::uint64_t raw_id = 0; raw_id < Traits::local_tile_count; ++raw_id) {
    const auto tile = LocalTileId{raw_id};
    const auto offset = static_cast<std::size_t>(raw_id);
    if (!static_cast<bool>(passable[offset]) ||
        topology.region_ids_[offset] != invalid_local_region) {
      continue;
    }

    const auto region_id =
        LocalRegionId{static_cast<std::uint32_t>(topology.regions_.size() + 1)};
    topology.regions_.push_back(LocalRegion{region_id});
    scratch.stack_.push_back(tile);
    topology.region_ids_[offset] = region_id;

    while (!scratch.stack_.empty()) {
      const auto current = scratch.stack_.back();
      scratch.stack_.pop_back();
      const auto local = detail::local_tile_coord<Shape>(current);
      const auto coord = tess::coord<Shape>(topology.chunk_coord_, current);
      auto& region = topology.regions_.back();
      detail::include_coord_in_bounds(region, coord);
      ++region.tile_count;
      ++passable_tiles;
      detail::add_boundary_exits<Shape>(topology.boundary_exits_,
                                        topology.chunk_coord_, region, current,
                                        local, coord);

      detail::for_each_local_axis_neighbor<Shape>(
          local, [&](LocalCoord3 neighbor_coord) {
            const auto neighbor = local_tile_id<Shape>(neighbor_coord);
            const auto neighbor_offset =
                static_cast<std::size_t>(neighbor.value);
            if (!static_cast<bool>(passable[neighbor_offset]) ||
                topology.region_ids_[neighbor_offset] != invalid_local_region) {
              return;
            }
            topology.region_ids_[neighbor_offset] = region_id;
            scratch.stack_.push_back(neighbor);
          });
    }
  }

  return LocalTopologyResult{
      TopologyStatus::Built,           topology.regions_.size(), passable_tiles,
      topology.boundary_exits_.size(), topology.version_,
  };
}

template <typename World, typename PassableTag>
auto build_region_graph(const World& world, LocalTopologyScratch& scratch,
                        RegionGraphT<typename World::residency_type>& graph)
    -> LocalTopologyResult {
  using Shape = typename World::shape_type;
  using Traits = ShapeTraits<Shape>;

  graph.clear();
  auto result = LocalTopologyResult{};

  if constexpr (std::is_same_v<typename World::residency_type,
                               AlwaysResident>) {
    graph.local_topologies_.resize(
        static_cast<std::size_t>(Traits::chunk_count));
    for (std::uint64_t raw_chunk = 0; raw_chunk < Traits::chunk_count;
         ++raw_chunk) {
      auto& topology =
          graph.local_topologies_[static_cast<std::size_t>(raw_chunk)];
      const auto local_result = build_local_chunk_topology<World, PassableTag>(
          world, ChunkKey{raw_chunk}, scratch, topology);
      if (local_result.status != TopologyStatus::Built) {
        result.status = local_result.status;
        graph.rebuild_region_index();
        return result;
      }
      result.region_count += local_result.region_count;
      result.passable_tile_count += local_result.passable_tile_count;
      result.boundary_exit_count += local_result.boundary_exit_count;
      result.version += local_result.version;
    }
  } else {
    // Sparse: build only over the resident set, sized by resident_count, never
    // chunk_count. Freeze the resident keys sorted ascending so a local index
    // equals chunk order; portals then append in chunk order exactly as the
    // dense build does, keeping "incremental == fresh" trivially.
    auto& keys = graph.sparse_.topology_keys_;
    const auto resident = world.resident_chunk_keys();
    keys.assign(resident.begin(), resident.end());
    std::sort(keys.begin(), keys.end(),
              [](ChunkKey lhs, ChunkKey rhs) { return lhs.value < rhs.value; });
    const auto count = keys.size();
    graph.local_topologies_.resize(count);
    graph.sparse_.frozen_generations_.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
      const auto local_result = build_local_chunk_topology<World, PassableTag>(
          world, keys[i], scratch, graph.local_topologies_[i]);
      // Resident keys are always in-world, so InvalidChunk cannot arise; keep
      // the status propagation for symmetry with the dense build.
      if (local_result.status != TopologyStatus::Built) {
        result.status = local_result.status;
        graph.rebuild_region_index();
        return result;
      }
      result.region_count += local_result.region_count;
      result.passable_tile_count += local_result.passable_tile_count;
      result.boundary_exit_count += local_result.boundary_exit_count;
      result.version += local_result.version;
      graph.sparse_.frozen_generations_[i] =
          world.residency_generation(keys[i]);
    }
  }

  for (const auto& topology : graph.local_topologies_) {
    detail::append_chunk_portals<Shape>(graph, topology, graph.portals_);
  }
  graph.rebuild_region_index();

  return result;
}

// Incrementally patches an already-built region graph after passability
// edits confined to `dirty_chunks`. Rebuilds local topology for each dirty
// chunk, re-derives portals for dirty chunks and their face neighbors, and
// restores the canonical full-build portal order, so the resulting graph is
// identical to a fresh build_region_graph over the edited world. An empty
// dirty set leaves the graph untouched. Returns the aggregate
// LocalTopologyResult over all chunks, mirroring build_region_graph. If the
// graph was not built for this world shape, falls back to a full build.
template <typename World, typename PassableTag>
auto update_region_graph(const World& world, LocalTopologyScratch& scratch,
                         RegionGraphT<typename World::residency_type>& graph,
                         std::span<const ChunkKey> dirty_chunks)
    -> LocalTopologyResult {
  using Shape = typename World::shape_type;
  using Traits = ShapeTraits<Shape>;

  if constexpr (std::is_same_v<typename World::residency_type,
                               AlwaysResident>) {
    const auto chunk_count = static_cast<std::size_t>(Traits::chunk_count);
    if (graph.local_topologies_.size() != chunk_count) {
      return build_region_graph<World, PassableTag>(world, scratch, graph);
    }
    for (const auto chunk : dirty_chunks) {
      if (chunk.value >= Traits::chunk_count) {
        return LocalTopologyResult{TopologyStatus::InvalidChunk, 0, 0, 0, 0};
      }
    }

    if (!dirty_chunks.empty()) {
      // Mark dirty chunks, then widen to every face neighbor: those are the
      // only chunks whose outgoing portals can reference a dirty chunk.
      std::vector<std::uint8_t> dirty(chunk_count, 0);
      std::vector<std::uint8_t> affected(chunk_count, 0);
      for (const auto chunk : dirty_chunks) {
        const auto offset = static_cast<std::size_t>(chunk.value);
        dirty[offset] = 1;
        affected[offset] = 1;
      }
      for (std::size_t raw_chunk = 0; raw_chunk < chunk_count; ++raw_chunk) {
        if (dirty[raw_chunk] == 0) {
          continue;
        }
        detail::for_each_face_neighbor_chunk<Shape>(
            chunk_coord<Shape>(ChunkKey{raw_chunk}), [&](ChunkCoord3 neighbor) {
              const auto key = chunk_key<Shape>(neighbor);
              affected[static_cast<std::size_t>(key.value)] = 1;
            });
      }

      for (std::size_t raw_chunk = 0; raw_chunk < chunk_count; ++raw_chunk) {
        if (dirty[raw_chunk] == 0) {
          continue;
        }
        build_local_chunk_topology<World, PassableTag>(
            world, ChunkKey{raw_chunk}, scratch,
            graph.local_topologies_[raw_chunk]);
      }

      // Every invalidated portal originates from an affected chunk, because
      // portals only span face-adjacent chunks. Drop them in one filtered
      // pass, re-derive all portals of affected chunks in exit order, then
      // stable-sort by from-chunk to restore the canonical build order.
      std::erase_if(graph.portals_, [&](const RegionPortal& portal) {
        return affected[static_cast<std::size_t>(portal.from.chunk.value)] != 0;
      });
      for (std::size_t raw_chunk = 0; raw_chunk < chunk_count; ++raw_chunk) {
        if (affected[raw_chunk] == 0) {
          continue;
        }
        detail::append_chunk_portals<Shape>(
            graph, graph.local_topologies_[raw_chunk], graph.portals_);
      }
      std::stable_sort(graph.portals_.begin(), graph.portals_.end(),
                       [](const RegionPortal& lhs, const RegionPortal& rhs) {
                         return lhs.from.chunk.value < rhs.from.chunk.value;
                       });
      graph.rebuild_region_index();
    }
  } else {
    // Sparse: any residency change since build forces a full rebuild (the graph
    // is frozen to a residency snapshot). Exact set-equality via resident_count
    // plus per-key generation: an evicted key reads generation 0, a reloaded
    // key gets a strictly greater monotonic generation, so equal count with all
    // frozen keys still at their frozen generation forces set identity.
    const auto count = graph.local_topologies_.size();
    if (count != world.resident_count() ||
        graph.sparse_.frozen_generations_.size() != world.resident_count()) {
      return build_region_graph<World, PassableTag>(world, scratch, graph);
    }
    for (std::size_t i = 0; i < count; ++i) {
      if (world.residency_generation(graph.sparse_.topology_keys_[i]) !=
          graph.sparse_.frozen_generations_[i]) {
        return build_region_graph<World, PassableTag>(world, scratch, graph);
      }
    }
    for (const auto chunk : dirty_chunks) {
      if (chunk.value >= Traits::chunk_count) {
        return LocalTopologyResult{TopologyStatus::InvalidChunk, 0, 0, 0, 0};
      }
    }

    if (!dirty_chunks.empty()) {
      // dirty/affected live in local-index space (size N = resident_count),
      // never chunk_count. A dirty chunk that is not resident holds no topology
      // in the frozen graph, so it is skipped.
      std::vector<std::uint8_t> dirty(count, 0);
      std::vector<std::uint8_t> affected(count, 0);
      for (const auto chunk : dirty_chunks) {
        const auto li = graph.local_index(chunk);
        if (li == graph.npos) {
          continue;
        }
        dirty[li] = 1;
        affected[li] = 1;
      }
      for (std::size_t i = 0; i < count; ++i) {
        if (dirty[i] == 0) {
          continue;
        }
        detail::for_each_face_neighbor_chunk<Shape>(
            chunk_coord<Shape>(graph.sparse_.topology_keys_[i]),
            [&](ChunkCoord3 neighbor) {
              const auto li = graph.local_index(chunk_key<Shape>(neighbor));
              if (li != graph.npos) {
                affected[li] = 1;
              }
            });
      }

      for (std::size_t i = 0; i < count; ++i) {
        if (dirty[i] == 0) {
          continue;
        }
        build_local_chunk_topology<World, PassableTag>(
            world, graph.sparse_.topology_keys_[i], scratch,
            graph.local_topologies_[i]);
      }

      std::erase_if(graph.portals_, [&](const RegionPortal& portal) {
        const auto li = graph.local_index(portal.from.chunk);
        return li != graph.npos && affected[li] != 0;
      });
      for (std::size_t i = 0; i < count; ++i) {
        if (affected[i] == 0) {
          continue;
        }
        detail::append_chunk_portals<Shape>(graph, graph.local_topologies_[i],
                                            graph.portals_);
      }
      std::stable_sort(graph.portals_.begin(), graph.portals_.end(),
                       [](const RegionPortal& lhs, const RegionPortal& rhs) {
                         return lhs.from.chunk.value < rhs.from.chunk.value;
                       });
      graph.rebuild_region_index();
    }
  }

  auto result = LocalTopologyResult{};
  for (const auto& topology : graph.local_topologies_) {
    result.region_count += topology.regions().size();
    for (const auto& region : topology.regions()) {
      result.passable_tile_count += region.tile_count;
    }
    result.boundary_exit_count += topology.boundary_exits().size();
    result.version += topology.version();
  }
  return result;
}

template <typename Shape, typename Residency>
auto reachable(const RegionGraphT<Residency>& graph, Coord3 start, Coord3 goal,
               RegionGraphScratch& scratch) -> ReachabilityResult {
  if (!contains<Shape>(start)) {
    return ReachabilityResult{ReachabilityStatus::InvalidStart, 0};
  }
  if (!contains<Shape>(goal)) {
    return ReachabilityResult{ReachabilityStatus::InvalidGoal, 0};
  }

  const auto start_region = graph.template region_of<Shape>(start);
  if (start_region.region == invalid_local_region) {
    if constexpr (!std::is_same_v<Residency, AlwaysResident>) {
      // A non-resident endpoint cannot be answered: its region is unknown,
      // distinct from a resident-but-walled tile (InvalidStart below).
      if (!graph.has_chunk(chunk_key<Shape>(chunk_coord<Shape>(start)))) {
        return ReachabilityResult{ReachabilityStatus::Indeterminate, 0};
      }
    }
    return ReachabilityResult{ReachabilityStatus::InvalidStart, 0};
  }
  const auto goal_region = graph.template region_of<Shape>(goal);
  if (goal_region.region == invalid_local_region) {
    if constexpr (!std::is_same_v<Residency, AlwaysResident>) {
      if (!graph.has_chunk(chunk_key<Shape>(chunk_coord<Shape>(goal)))) {
        return ReachabilityResult{ReachabilityStatus::Indeterminate, 0};
      }
    }
    return ReachabilityResult{ReachabilityStatus::InvalidGoal, 0};
  }
  if (start_region == goal_region) {
    return ReachabilityResult{ReachabilityStatus::Reachable, 1};
  }

  const auto start_index = graph.region_index(start_region);
  if (start_index == invalid_region_index) {
    return ReachabilityResult{ReachabilityStatus::InvalidStart, 0};
  }
  const auto goal_index = graph.region_index(goal_region);
  if (goal_index == invalid_region_index) {
    return ReachabilityResult{ReachabilityStatus::InvalidGoal, 0};
  }

  scratch.begin_traversal(static_cast<std::size_t>(graph.region_count()));
  scratch.visit(start_index);
  std::size_t visited_count = 1;
  scratch.frontier_.push_back(start_index);

  // Sparse: track whether the searched component touches a region that exits
  // into a non-resident chunk, so an exhausted BFS that never reached goal
  // returns Indeterminate rather than a wrong Unreachable.
  [[maybe_unused]] bool touched_missing = false;
  if constexpr (!std::is_same_v<Residency, AlwaysResident>) {
    touched_missing =
        graph.sparse_
            .region_reaches_missing_[static_cast<std::size_t>(start_index)] !=
        0;
  }

  while (!scratch.frontier_.empty()) {
    const auto current = scratch.frontier_.back();
    scratch.frontier_.pop_back();

    const auto begin = static_cast<std::size_t>(
        graph.adjacency_starts_[static_cast<std::size_t>(current)]);
    const auto end = static_cast<std::size_t>(
        graph.adjacency_starts_[static_cast<std::size_t>(current) + 1]);
    for (std::size_t edge = begin; edge < end; ++edge) {
      const auto target = graph.adjacency_targets_[edge];
      if (scratch.is_visited(target)) {
        continue;
      }
      if (target == goal_index) {
        return ReachabilityResult{ReachabilityStatus::Reachable,
                                  visited_count + 1};
      }
      scratch.visit(target);
      ++visited_count;
      scratch.frontier_.push_back(target);
      if constexpr (!std::is_same_v<Residency, AlwaysResident>) {
        touched_missing =
            touched_missing ||
            graph.sparse_.region_reaches_missing_[static_cast<std::size_t>(
                target)] != 0;
      }
    }
  }

  if constexpr (!std::is_same_v<Residency, AlwaysResident>) {
    if (touched_missing) {
      return ReachabilityResult{ReachabilityStatus::Indeterminate,
                                visited_count};
    }
  }
  return ReachabilityResult{ReachabilityStatus::Unreachable, visited_count};
}

// Reports whether `graph` still matches `world` -- i.e. whether a reachability
// query on it would reflect the world's current topology. A precheck MUST
// consult this and fall back to A* when it returns false: a STALE graph can
// return a definitive (but wrong) Unreachable from an outdated snapshot. Const
// and non-mutating -- it recomputes the same staleness test update_region_graph
// applies, WITHOUT triggering a rebuild. Allocation-free; O(chunk_count) dense,
// O(resident_count) sparse (never scans non-resident chunks).
template <typename World>
[[nodiscard]] auto is_region_graph_fresh(
    const World& world,
    const RegionGraphT<typename World::residency_type>& graph) noexcept
    -> bool {
  using Residency = typename World::residency_type;
  if constexpr (std::is_same_v<Residency, AlwaysResident>) {
    // Dense: every chunk's stored topology version must still be current. A
    // graph that was never built (or built for a different shape) is not fresh.
    if (graph.local_topologies_.size() != World::chunk_count) {
      return false;
    }
    for (std::uint64_t c = 0; c < World::chunk_count; ++c) {
      if (graph.local_topologies_[static_cast<std::size_t>(c)].version() !=
          world.meta(ChunkKey{c}).topology_version) {
        return false;
      }
    }
    return true;
  } else {
    // Sparse: the frozen residency snapshot must still hold (resident_count
    // plus per-key generation -- an evicted key reads generation 0, a reloaded
    // key a strictly greater one), AND every resident chunk's topology version
    // must still be current (an in-place edit). The generation is checked first
    // so meta()/version reads only ever touch a still-resident key.
    const auto count = graph.local_topologies_.size();
    if (count != world.resident_count() ||
        graph.sparse_.frozen_generations_.size() != world.resident_count()) {
      return false;
    }
    for (std::size_t i = 0; i < count; ++i) {
      const auto key = graph.sparse_.topology_keys_[i];
      if (world.residency_generation(key) !=
          graph.sparse_.frozen_generations_[i]) {
        return false;
      }
      if (graph.local_topologies_[i].version() !=
          world.meta(key).topology_version) {
        return false;
      }
    }
    return true;
  }
}

}  // namespace tess
