#pragma once

#include <tess/core/shape.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace tess {

struct LocalRegionId {
  std::uint32_t value = 0;

  friend constexpr bool operator==(LocalRegionId lhs,
                                   LocalRegionId rhs) noexcept = default;
};

inline constexpr LocalRegionId invalid_local_region{};

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
  LocalRegionId id;
  std::size_t tile_count = 0;
  Box3 bounds{};
  std::size_t boundary_exit_count = 0;
};

struct LocalBoundaryExit {
  LocalRegionId region;
  LocalTileId local_tile;
  Coord3 coord;
  BoundaryFace face;
  ChunkKey target_chunk;
};

struct LocalTopologyResult {
  TopologyStatus status = TopologyStatus::Built;
  std::size_t region_count = 0;
  std::size_t passable_tile_count = 0;
  std::size_t boundary_exit_count = 0;
  std::uint32_t version = 0;
};

struct RegionRef {
  ChunkKey chunk;
  LocalRegionId region;

  friend constexpr bool operator==(RegionRef lhs,
                                   RegionRef rhs) noexcept = default;
};

struct RegionPortal {
  RegionRef from;
  RegionRef to;
  Coord3 from_coord;
  Coord3 to_coord;
  BoundaryFace face;
};

enum class ReachabilityStatus : std::uint8_t {
  Reachable,
  Unreachable,
  InvalidStart,
  InvalidGoal,
};

struct ReachabilityResult {
  ReachabilityStatus status;
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
    visited_.reserve(count);
  }

  [[nodiscard]] auto capacity() const noexcept -> std::size_t {
    return frontier_.capacity();
  }

 private:
  template <typename Shape>
  friend auto reachable(const class RegionGraph& graph, Coord3 start,
                        Coord3 goal, RegionGraphScratch& scratch)
      -> ReachabilityResult;

  std::vector<RegionRef> frontier_;
  std::vector<RegionRef> visited_;
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

class RegionGraph {
 public:
  void clear() noexcept {
    local_topologies_.clear();
    portals_.clear();
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
    if (chunk.value >= local_topologies_.size()) {
      return nullptr;
    }
    return &local_topologies_[static_cast<std::size_t>(chunk.value)];
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

 private:
  template <typename World, typename PassableTag>
  friend auto build_region_graph(const World& world,
                                 LocalTopologyScratch& scratch,
                                 RegionGraph& graph) -> LocalTopologyResult;

  std::vector<LocalChunkTopology> local_topologies_;
  std::vector<RegionPortal> portals_;
};

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

[[nodiscard]] constexpr auto is_visited(std::span<const RegionRef> visited,
                                        RegionRef region) noexcept -> bool {
  for (const auto item : visited) {
    if (item == region) {
      return true;
    }
  }
  return false;
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
                        RegionGraph& graph) -> LocalTopologyResult {
  using Shape = typename World::shape_type;
  using Traits = ShapeTraits<Shape>;

  graph.clear();
  graph.local_topologies_.resize(static_cast<std::size_t>(Traits::chunk_count));

  auto result = LocalTopologyResult{};
  for (std::uint64_t raw_chunk = 0; raw_chunk < Traits::chunk_count;
       ++raw_chunk) {
    auto& topology =
        graph.local_topologies_[static_cast<std::size_t>(raw_chunk)];
    const auto local_result = build_local_chunk_topology<World, PassableTag>(
        world, ChunkKey{raw_chunk}, scratch, topology);
    if (local_result.status != TopologyStatus::Built) {
      result.status = local_result.status;
      return result;
    }
    result.region_count += local_result.region_count;
    result.passable_tile_count += local_result.passable_tile_count;
    result.boundary_exit_count += local_result.boundary_exit_count;
    result.version += local_result.version;
  }

  for (const auto& topology : graph.local_topologies_) {
    for (const auto& exit : topology.boundary_exits()) {
      const auto to_coord = detail::neighbor_coord(exit.coord, exit.face);
      const auto target = graph.region_of<Shape>(to_coord);
      if (target.region == invalid_local_region) {
        continue;
      }
      graph.portals_.push_back(RegionPortal{
          RegionRef{topology.chunk(), exit.region},
          target,
          exit.coord,
          to_coord,
          exit.face,
      });
    }
  }

  return result;
}

template <typename Shape>
auto reachable(const RegionGraph& graph, Coord3 start, Coord3 goal,
               RegionGraphScratch& scratch) -> ReachabilityResult {
  if (!contains<Shape>(start)) {
    return ReachabilityResult{ReachabilityStatus::InvalidStart, 0};
  }
  if (!contains<Shape>(goal)) {
    return ReachabilityResult{ReachabilityStatus::InvalidGoal, 0};
  }

  const auto start_region = graph.region_of<Shape>(start);
  if (start_region.region == invalid_local_region) {
    return ReachabilityResult{ReachabilityStatus::InvalidStart, 0};
  }
  const auto goal_region = graph.region_of<Shape>(goal);
  if (goal_region.region == invalid_local_region) {
    return ReachabilityResult{ReachabilityStatus::InvalidGoal, 0};
  }
  if (start_region == goal_region) {
    return ReachabilityResult{ReachabilityStatus::Reachable, 1};
  }

  scratch.frontier_.clear();
  scratch.visited_.clear();
  scratch.frontier_.push_back(start_region);
  scratch.visited_.push_back(start_region);

  while (!scratch.frontier_.empty()) {
    const auto current = scratch.frontier_.back();
    scratch.frontier_.pop_back();

    for (const auto& portal : graph.portals()) {
      if (portal.from != current ||
          detail::is_visited(
              std::span<const RegionRef>{scratch.visited_.data(),
                                         scratch.visited_.size()},
              portal.to)) {
        continue;
      }
      if (portal.to == goal_region) {
        return ReachabilityResult{ReachabilityStatus::Reachable,
                                  scratch.visited_.size() + 1};
      }
      scratch.visited_.push_back(portal.to);
      scratch.frontier_.push_back(portal.to);
    }
  }

  return ReachabilityResult{ReachabilityStatus::Unreachable,
                            scratch.visited_.size()};
}

}  // namespace tess
