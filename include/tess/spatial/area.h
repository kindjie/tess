#pragma once

#include <tess/topology/topology.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <span>
#include <utility>
#include <vector>

namespace tess {

/// Stable identifier within one built `AreaIndex`; zero means no area.
struct AreaId {
  std::uint32_t value = 0;

  friend constexpr bool operator==(AreaId lhs, AreaId rhs) noexcept = default;
};

/// Sentinel returned for an ungrouped or invalid region.
inline constexpr AreaId invalid_area_id{};

/// Aggregates regions that a caller assigned the same nonzero key.
struct AreaSummary {
  AreaId id{};
  std::uint64_t key = 0;
  std::size_t region_count = 0;
  std::size_t tile_count = 0;
  Box3 bounds{};
};

/// Undirected area adjacency summarized from directed region portals.
struct AreaConnection {
  AreaId first{};
  AreaId second{};
  std::size_t directed_portal_count = 0;
};

/// Outcome of rebuilding a caller-defined area index.
enum class AreaBuildStatus : std::uint8_t {
  Built,
  TooManyAreas,
};

/// Counts returned by `build_area_index`.
struct AreaBuildResult {
  AreaBuildStatus status = AreaBuildStatus::Built;
  std::size_t area_count = 0;
  std::size_t connection_count = 0;
};

/// Reusable temporary storage for area grouping and adjacency construction.
class AreaIndexScratch {
 public:
  void reserve(std::size_t region_count, std::size_t portal_count) {
    region_keys_.reserve(region_count);
    unique_keys_.reserve(region_count);
    edge_keys_.reserve(portal_count);
  }

 private:
  template <typename Residency, typename Grouper>
  friend auto build_area_index(const RegionGraphT<Residency>& graph,
                               Grouper&& grouper, AreaIndexScratch& scratch,
                               class AreaIndex& index) -> AreaBuildResult;

  std::vector<std::uint64_t> region_keys_;
  std::vector<std::uint64_t> unique_keys_;
  std::vector<std::uint64_t> edge_keys_;
};

namespace detail {

inline void extend_area_bounds(Box3& current, Box3 addition,
                               bool first) noexcept {
  if (first) {
    current = addition;
    return;
  }
  const auto current_end = Coord3{
      current.origin.x + static_cast<std::int64_t>(current.extent.x),
      current.origin.y + static_cast<std::int64_t>(current.extent.y),
      current.origin.z + static_cast<std::int64_t>(current.extent.z),
  };
  const auto addition_end = Coord3{
      addition.origin.x + static_cast<std::int64_t>(addition.extent.x),
      addition.origin.y + static_cast<std::int64_t>(addition.extent.y),
      addition.origin.z + static_cast<std::int64_t>(addition.extent.z),
  };
  const auto origin = Coord3{
      std::min(current.origin.x, addition.origin.x),
      std::min(current.origin.y, addition.origin.y),
      std::min(current.origin.z, addition.origin.z),
  };
  const auto end = Coord3{
      std::max(current_end.x, addition_end.x),
      std::max(current_end.y, addition_end.y),
      std::max(current_end.z, addition_end.z),
  };
  current = Box3{
      origin,
      Extent3{static_cast<std::uint64_t>(end.x - origin.x),
              static_cast<std::uint64_t>(end.y - origin.y),
              static_cast<std::uint64_t>(end.z - origin.z)},
  };
}

}  // namespace detail

/// Caller-defined, graph-derived grouping of topology regions.
class AreaIndex {
 public:
  void reserve(std::size_t region_count, std::size_t portal_count) {
    region_refs_.reserve(region_count);
    region_areas_.reserve(region_count);
    areas_.reserve(region_count);
    connections_.reserve(portal_count);
  }

  void clear() noexcept {
    region_refs_.clear();
    region_areas_.clear();
    areas_.clear();
    connections_.clear();
    graph_identity_ = nullptr;
    graph_revision_ = 0;
  }

  [[nodiscard]] auto areas() const noexcept -> std::span<const AreaSummary> {
    return areas_;
  }

  [[nodiscard]] auto connections() const noexcept
      -> std::span<const AreaConnection> {
    return connections_;
  }

  [[nodiscard]] auto area_of(RegionRef region) const noexcept -> AreaId {
    const auto it =
        std::lower_bound(region_refs_.begin(), region_refs_.end(), region,
                         [](RegionRef lhs, RegionRef rhs) {
                           return lhs.chunk.value < rhs.chunk.value ||
                                  (lhs.chunk.value == rhs.chunk.value &&
                                   lhs.region.value < rhs.region.value);
                         });
    if (it == region_refs_.end() || *it != region) {
      return invalid_area_id;
    }
    return region_areas_[static_cast<std::size_t>(it - region_refs_.begin())];
  }

  template <typename Shape, typename Residency>
  [[nodiscard]] auto area_of(const RegionGraphT<Residency>& graph,
                             Coord3 coord) const noexcept -> AreaId {
    if (!is_valid(graph)) {
      return invalid_area_id;
    }
    return area_of(graph.template region_of<Shape>(coord));
  }

  template <typename Residency>
  [[nodiscard]] auto is_valid(
      const RegionGraphT<Residency>& graph) const noexcept -> bool {
    return graph_identity_ == static_cast<const void*>(&graph) &&
           graph_revision_ == graph.revision();
  }

 private:
  template <typename Residency, typename Grouper>
  friend auto build_area_index(const RegionGraphT<Residency>& graph,
                               Grouper&& grouper, AreaIndexScratch& scratch,
                               AreaIndex& index) -> AreaBuildResult;

  std::vector<RegionRef> region_refs_;
  std::vector<AreaId> region_areas_;
  std::vector<AreaSummary> areas_;
  std::vector<AreaConnection> connections_;
  const void* graph_identity_ = nullptr;
  std::uint64_t graph_revision_ = 0;
};

/// Groups graph regions by a caller-supplied nonzero 64-bit semantic key.
template <typename Residency, typename Grouper>
auto build_area_index(const RegionGraphT<Residency>& graph, Grouper&& grouper,
                      AreaIndexScratch& scratch, AreaIndex& index)
    -> AreaBuildResult {
  const auto region_count = static_cast<std::size_t>(graph.region_count());
  index.clear();
  scratch.region_keys_.assign(region_count, 0);
  scratch.unique_keys_.clear();
  index.region_refs_.resize(region_count);
  index.region_areas_.assign(region_count, invalid_area_id);

  for (const auto& topology : graph.local_topologies()) {
    for (const auto& region : topology.regions()) {
      const auto ref = RegionRef{topology.chunk(), region.id};
      const auto offset = graph.region_index(ref);
      if (offset == invalid_region_index) {
        continue;
      }
      const auto key =
          static_cast<std::uint64_t>(std::invoke(grouper, ref, region));
      index.region_refs_[offset] = ref;
      scratch.region_keys_[offset] = key;
      if (key != 0) {
        scratch.unique_keys_.push_back(key);
      }
    }
  }

  std::sort(scratch.unique_keys_.begin(), scratch.unique_keys_.end());
  scratch.unique_keys_.erase(
      std::unique(scratch.unique_keys_.begin(), scratch.unique_keys_.end()),
      scratch.unique_keys_.end());
  if (scratch.unique_keys_.size() >= invalid_region_index) {
    return {AreaBuildStatus::TooManyAreas, 0, 0};
  }
  index.areas_.resize(scratch.unique_keys_.size());
  for (std::size_t i = 0; i < scratch.unique_keys_.size(); ++i) {
    index.areas_[i].id = AreaId{static_cast<std::uint32_t>(i + 1)};
    index.areas_[i].key = scratch.unique_keys_[i];
  }

  for (const auto& topology : graph.local_topologies()) {
    for (const auto& region : topology.regions()) {
      const auto ref = RegionRef{topology.chunk(), region.id};
      const auto offset = graph.region_index(ref);
      const auto key = scratch.region_keys_[offset];
      if (key == 0) {
        continue;
      }
      const auto key_it = std::lower_bound(scratch.unique_keys_.begin(),
                                           scratch.unique_keys_.end(), key);
      const auto area_offset =
          static_cast<std::size_t>(key_it - scratch.unique_keys_.begin());
      const auto id = AreaId{static_cast<std::uint32_t>(area_offset + 1)};
      index.region_areas_[offset] = id;
      auto& area = index.areas_[area_offset];
      detail::extend_area_bounds(area.bounds, region.bounds,
                                 area.region_count == 0);
      ++area.region_count;
      area.tile_count += region.tile_count;
    }
  }

  scratch.edge_keys_.clear();
  for (const auto& portal : graph.portals()) {
    const auto from = index.region_areas_[graph.region_index(portal.from)];
    const auto to = index.region_areas_[graph.region_index(portal.to)];
    if (from == invalid_area_id || to == invalid_area_id || from == to) {
      continue;
    }
    const auto first = std::min(from.value, to.value);
    const auto second = std::max(from.value, to.value);
    scratch.edge_keys_.push_back((static_cast<std::uint64_t>(first) << 32U) |
                                 second);
  }
  std::sort(scratch.edge_keys_.begin(), scratch.edge_keys_.end());
  for (std::size_t begin = 0; begin < scratch.edge_keys_.size();) {
    auto end = begin + 1;
    while (end < scratch.edge_keys_.size() &&
           scratch.edge_keys_[end] == scratch.edge_keys_[begin]) {
      ++end;
    }
    const auto edge = scratch.edge_keys_[begin];
    index.connections_.push_back(
        AreaConnection{AreaId{static_cast<std::uint32_t>(edge >> 32U)},
                       AreaId{static_cast<std::uint32_t>(edge)}, end - begin});
    begin = end;
  }

  index.graph_identity_ = static_cast<const void*>(&graph);
  index.graph_revision_ = graph.revision();
  return {AreaBuildStatus::Built, index.areas_.size(),
          index.connections_.size()};
}

}  // namespace tess
