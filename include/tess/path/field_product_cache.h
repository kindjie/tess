#pragma once

#include <tess/path/path.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace tess {

class GoalSet {
 public:
  void reserve(std::size_t count) { goals_.reserve(count); }

  void clear() noexcept { goals_.clear(); }

  void add(Coord3 goal) { goals_.push_back(goal); }

  [[nodiscard]] auto empty() const noexcept -> bool { return goals_.empty(); }

  [[nodiscard]] auto size() const noexcept -> std::size_t {
    return goals_.size();
  }

  [[nodiscard]] auto goals() const noexcept -> std::span<const Coord3> {
    return goals_;
  }

 private:
  std::vector<Coord3> goals_;
};

struct NearestTargetResult {
  PathStatus status = PathStatus::NoPath;
  std::uint32_t cost = 0;
  Coord3 target{};
  std::size_t expanded_nodes = 0;
  std::size_t reached_nodes = 0;
  std::span<const Coord3> path;
};

class DistanceFieldProduct {
 public:
  void reserve_goals(std::size_t count) { goals_.reserve(count); }

  void reserve_nodes(std::size_t node_count) { distance_.reserve(node_count); }

  void reserve_dependencies(std::size_t count) { dependencies_.reserve(count); }

  void clear() noexcept {
    status_ = PathStatus::NoPath;
    expanded_nodes_ = 0;
    reached_nodes_ = 0;
    tile_count_ = 0;
    chunk_count_ = 0;
    local_tile_count_ = 0;
    goals_.clear();
    distance_.clear();
    dependencies_.clear();
  }

  // See WeightedRouteProduct::is_valid: empty dependencies are invalid by
  // definition so cleared/never-built products never replay vacuously.
  template <typename World>
  [[nodiscard]] auto is_valid(const World& world) const noexcept -> bool {
    return !dependencies_.empty() && dependencies_.is_valid(world);
  }

  [[nodiscard]] auto status() const noexcept -> PathStatus { return status_; }

  [[nodiscard]] auto goals() const noexcept -> std::span<const Coord3> {
    return goals_;
  }

  [[nodiscard]] auto dependencies() const noexcept
      -> std::span<const ChunkVersionDependencies::ChunkVersionDependency> {
    return dependencies_.chunks();
  }

  [[nodiscard]] auto expanded_nodes() const noexcept -> std::size_t {
    return expanded_nodes_;
  }

  [[nodiscard]] auto reached_nodes() const noexcept -> std::size_t {
    return reached_nodes_;
  }

  [[nodiscard]] auto byte_size() const noexcept -> std::size_t {
    return distance_.size() * sizeof(std::uint32_t) +
           goals_.size() * sizeof(Coord3) +
           dependencies_.size() *
               sizeof(ChunkVersionDependencies::ChunkVersionDependency);
  }

 private:
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

  [[nodiscard]] auto is_goal(Coord3 coord) const noexcept -> bool {
    for (const auto goal : goals_) {
      if (goal == coord) {
        return true;
      }
    }
    return false;
  }

  PathStatus status_ = PathStatus::NoPath;
  std::size_t expanded_nodes_ = 0;
  std::size_t reached_nodes_ = 0;
  std::size_t tile_count_ = 0;
  std::uint64_t chunk_count_ = 0;
  std::uint64_t local_tile_count_ = 0;
  Extent3 shape_size_{};
  Extent3 chunk_extent_{};
  std::vector<Coord3> goals_;
  std::vector<std::uint32_t> distance_;
  ChunkVersionDependencies dependencies_;
};

struct FieldProductCacheStats {
  std::size_t entries = 0;
  std::size_t bytes = 0;
  std::size_t hits = 0;
  std::size_t misses = 0;
  std::size_t evictions = 0;
  std::size_t stale_rejections = 0;
};

// Entries validate against world content versions, not a world instance:
// keep one cache per world (see RouteCacheScratch fingerprint notes for the
// aliasing mechanism).
class FieldProductCache {
 public:
  explicit FieldProductCache(
      std::size_t byte_budget =
          std::numeric_limits<std::size_t>::max()) noexcept
      : byte_budget_(byte_budget) {}

  void set_byte_budget(std::size_t byte_budget) {
    byte_budget_ = byte_budget;
    evict_to_budget();
  }

  void reserve_entries(std::size_t count) { entries_.reserve(count); }

  void clear() noexcept {
    entries_.clear();
    bytes_ = 0;
  }

  void reset_stats() noexcept {
    hits_ = 0;
    misses_ = 0;
    evictions_ = 0;
    stale_rejections_ = 0;
  }

  [[nodiscard]] auto stats() const noexcept -> FieldProductCacheStats {
    return FieldProductCacheStats{
        entries_.size(), bytes_, hits_, misses_, evictions_, stale_rejections_,
    };
  }

  // The returned pointer targets heap storage that never moves when other
  // entries are stored or evicted. It stays valid until a `store()` or
  // eviction touches this exact key, or until `clear()`. Stale products are
  // erased on lookup and reported as stale rejections.
  template <typename World, typename Tag>
  [[nodiscard]] auto lookup(const World& world, const GoalSet& goals)
      -> const DistanceFieldProduct* {
    for (std::size_t i = 0; i < entries_.size(); ++i) {
      auto& entry = entries_[i];
      if (!key_matches<World, Tag>(entry.key, goals.goals())) {
        continue;
      }
      if (!entry.product->is_valid(world)) {
        ++stale_rejections_;
        erase_entry(i);
        return nullptr;
      }
      ++hits_;
      entry.last_used = ++clock_;
      return entry.product.get();
    }

    ++misses_;
    return nullptr;
  }

  // Takes ownership of `product` by move; world-sized field data is never
  // copied. The argument is left moved-from (empty but reusable). A product
  // whose entry exceeds the byte budget cannot be cached at all; that store
  // clears the entire cache and returns false.
  template <typename World, typename Tag>
  auto store(DistanceFieldProduct&& product) -> bool {
    if (product.status() != PathStatus::Found) {
      return false;
    }

    auto key = make_key<World, Tag>(product.goals());
    const auto bytes = entry_byte_size(key, product);
    if (bytes > byte_budget_) {
      clear();
      return false;
    }

    for (std::size_t i = 0; i < entries_.size(); ++i) {
      if (keys_equal(entries_[i].key, key)) {
        bytes_ -= entries_[i].bytes;
        *entries_[i].product = std::move(product);
        entries_[i].last_used = ++clock_;
        entries_[i].bytes = bytes;
        bytes_ += bytes;
        evict_to_budget();
        return true;
      }
    }

    auto& entry = entries_.emplace_back();
    entry.key = std::move(key);
    entry.product = std::make_unique<DistanceFieldProduct>(std::move(product));
    entry.last_used = ++clock_;
    entry.bytes = bytes;
    bytes_ += bytes;
    evict_to_budget();
    return true;
  }

 private:
  struct Key {
    std::uintptr_t passability_field = 0;
    std::size_t tile_count = 0;
    std::uint64_t chunk_count = 0;
    std::uint64_t local_tile_count = 0;
    Extent3 shape_size{};
    Extent3 chunk_extent{};
    std::vector<Coord3> goals;
  };

  // Each product lives behind a `unique_ptr` so `entries_` growth and
  // eviction of other entries never relocate a product that a caller still
  // borrows through `lookup()`.
  struct Entry {
    Key key;
    std::unique_ptr<DistanceFieldProduct> product;
    std::uint64_t last_used = 0;
    std::size_t bytes = 0;
  };

  template <typename Tag>
  [[nodiscard]] static auto tag_identity() noexcept -> std::uintptr_t {
    static const auto token = std::uint8_t{0};
    return reinterpret_cast<std::uintptr_t>(&token);
  }

  template <typename World, typename Tag>
  [[nodiscard]] static auto make_key(std::span<const Coord3> goals) -> Key {
    return Key{
        tag_identity<Tag>(),
        detail::tile_count<World>(),
        World::chunk_count,
        World::local_tile_count,
        ShapeTraits<typename World::shape_type>::size,
        ShapeTraits<typename World::shape_type>::chunk,
        std::vector<Coord3>{goals.begin(), goals.end()},
    };
  }

  [[nodiscard]] static auto keys_equal(const Key& lhs, const Key& rhs) noexcept
      -> bool {
    return lhs.passability_field == rhs.passability_field &&
           lhs.tile_count == rhs.tile_count &&
           lhs.chunk_count == rhs.chunk_count &&
           lhs.local_tile_count == rhs.local_tile_count &&
           lhs.shape_size == rhs.shape_size &&
           lhs.chunk_extent == rhs.chunk_extent && lhs.goals == rhs.goals;
  }

  template <typename World, typename Tag>
  [[nodiscard]] static auto key_matches(const Key& key,
                                        std::span<const Coord3> goals) noexcept
      -> bool {
    return key.passability_field == tag_identity<Tag>() &&
           key.tile_count == detail::tile_count<World>() &&
           key.chunk_count == World::chunk_count &&
           key.local_tile_count == World::local_tile_count &&
           key.shape_size == ShapeTraits<typename World::shape_type>::size &&
           key.chunk_extent == ShapeTraits<typename World::shape_type>::chunk &&
           key.goals.size() == goals.size() &&
           std::equal(key.goals.begin(), key.goals.end(), goals.begin());
  }

  [[nodiscard]] static auto entry_byte_size(
      const Key& key, const DistanceFieldProduct& product) noexcept
      -> std::size_t {
    return sizeof(Entry) + sizeof(DistanceFieldProduct) +
           key.goals.size() * sizeof(Coord3) + product.byte_size();
  }

  void erase_entry(std::size_t index) noexcept {
    bytes_ -= entries_[index].bytes;
    entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(index));
  }

  void evict_to_budget() noexcept {
    while (bytes_ > byte_budget_ && !entries_.empty()) {
      auto oldest = std::size_t{0};
      for (std::size_t i = 1; i < entries_.size(); ++i) {
        if (entries_[i].last_used < entries_[oldest].last_used) {
          oldest = i;
        }
      }
      erase_entry(oldest);
      ++evictions_;
    }
  }

  std::vector<Entry> entries_;
  std::size_t byte_budget_ = 0;
  std::size_t bytes_ = 0;
  std::size_t hits_ = 0;
  std::size_t misses_ = 0;
  std::size_t evictions_ = 0;
  std::size_t stale_rejections_ = 0;
  std::uint64_t clock_ = 0;
};

template <typename World, typename Tag>
auto build_distance_field_product(const World& world, const GoalSet& goals,
                                  DistanceFieldScratch& scratch,
                                  DistanceFieldProduct& product)
    -> DistanceFieldResult {
  using Shape = typename World::shape_type;
  // Sizes its distance arrays by the global tile count and treats missing
  // chunks as blocked with no MissingChunkPolicy, so on a sparse world it would
  // allocate for the whole (possibly astronomical) shape. Dense-only until the
  // distance-field family is ported to NodeIndexSpace; this also transitively
  // guards distance_field_product_path and nearest_target, which only consume
  // a product built here.
  static_assert(
      std::is_same_v<typename World::residency_type, AlwaysResident>,
      "build_distance_field_product is dense-only; the sparse distance-field "
      "slice lands later.");
  constexpr auto infinite_distance = std::numeric_limits<std::uint32_t>::max();

  TESS_DIAG_EVENT_VALUE(path_clear, scratch.touched_.size());
  scratch.clear_build();
  product.clear();
  product.goals_.assign(goals.goals().begin(), goals.goals().end());
  product.tile_count_ = detail::tile_count<World>();
  product.chunk_count_ = World::chunk_count;
  product.local_tile_count_ = World::local_tile_count;
  product.shape_size_ = ShapeTraits<Shape>::size;
  product.chunk_extent_ = ShapeTraits<Shape>::chunk;

  if (goals.empty()) {
    return DistanceFieldResult{PathStatus::InvalidGoal, 0, 0};
  }
  for (const auto goal : goals.goals()) {
    if (!contains<Shape>(goal)) {
      return DistanceFieldResult{PathStatus::InvalidGoal, 0, 0};
    }
    TESS_DIAG_EVENT(path_goal_passability_check);
    if (!detail::is_passable<World, Tag>(world, goal)) {
      return DistanceFieldResult{PathStatus::InvalidGoal, 0, 0};
    }
  }

  const auto node_count = detail::tile_count<World>();
  if (scratch.distance_.size() != node_count) {
    TESS_DIAG_EVENT(path_initialize);
    scratch.generation_.assign(node_count, 0);
    scratch.distance_.assign(node_count, infinite_distance);
  }

  for (const auto goal : goals.goals()) {
    const auto goal_index = detail::tile_index<Shape>(goal);
    const auto goal_offset = static_cast<std::size_t>(goal_index);
    if (scratch.is_current(goal_offset)) {
      continue;
    }
    scratch.distance_[goal_offset] = 0;
    scratch.touch_node(goal_index);
    TESS_DIAG_EVENT(path_touch_node);
    scratch.frontier_.push_back(goal_index);
    TESS_DIAG_EVENT(path_heap_push);
  }

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

  product.distance_.assign(node_count, infinite_distance);
  for (const auto index : scratch.touched_) {
    const auto offset = static_cast<std::size_t>(index);
    product.distance_[offset] = scratch.distance_[offset];
    const auto key = tile_key<Shape>(detail::tile_coord<Shape>(index));
    product.dependencies_.add_chunk(world, chunk_key<Shape>(key));
  }
  // The flood never touches a node inside a fully-blocked chunk, but an edit
  // that opens one changes reachability, so it must invalidate the product.
  // Axis moves cross at most one chunk face: every blocked tile adjacent to
  // the reached region lies in a touched chunk or one of its face
  // neighbors, so depending on those neighbors covers the whole blocked
  // frontier. Indexing re-reads chunks() each pass because add_chunk may
  // grow the underlying vector.
  {
    using Traits = ShapeTraits<Shape>;
    const auto touched_chunks = product.dependencies_.size();
    for (std::size_t i = 0; i < touched_chunks; ++i) {
      const auto center =
          chunk_coord<Shape>(product.dependencies_.chunks()[i].key);
      const auto add = [&](ChunkCoord3 neighbor) {
        product.dependencies_.add_chunk(world, chunk_key<Shape>(neighbor));
      };
      if (center.x > 0) {
        add(ChunkCoord3{center.x - 1, center.y, center.z});
      }
      if (center.x + 1 < Traits::chunk_count_x) {
        add(ChunkCoord3{center.x + 1, center.y, center.z});
      }
      if (center.y > 0) {
        add(ChunkCoord3{center.x, center.y - 1, center.z});
      }
      if (center.y + 1 < Traits::chunk_count_y) {
        add(ChunkCoord3{center.x, center.y + 1, center.z});
      }
      if (center.z > 0) {
        add(ChunkCoord3{center.x, center.y, center.z - 1});
      }
      if (center.z + 1 < Traits::chunk_count_z) {
        add(ChunkCoord3{center.x, center.y, center.z + 1});
      }
    }
  }
  product.status_ = PathStatus::Found;
  product.expanded_nodes_ = expanded_nodes;
  product.reached_nodes_ = scratch.touched_.size();

  return DistanceFieldResult{product.status_, product.expanded_nodes_,
                             product.reached_nodes_};
}

template <typename World, typename Tag>
auto distance_field_product_path(const World& world, Coord3 start,
                                 const DistanceFieldProduct& product,
                                 DistanceFieldScratch& scratch) -> PathResult {
  using Shape = typename World::shape_type;
  // Indexes product.distance_ by raw tile id, so it is dense-only until the
  // distance-field product family is ported to NodeIndexSpace. The single-goal
  // build_distance_field / distance_field_path pair is already sparse-capable.
  static_assert(
      std::is_same_v<typename World::residency_type, AlwaysResident>,
      "distance_field_product_path is dense-only; the sparse distance-field "
      "product slice lands later.");
  constexpr auto infinite_distance = std::numeric_limits<std::uint32_t>::max();

  scratch.clear_path();
  if (!contains<Shape>(start)) {
    return PathResult{PathStatus::InvalidStart, 0, 0, 0, scratch.path_};
  }
  TESS_DIAG_EVENT(path_start_passability_check);
  if (!detail::is_passable<World, Tag>(world, start)) {
    return PathResult{PathStatus::InvalidStart, 0, 0, 0, scratch.path_};
  }
  if (product.status_ != PathStatus::Found || !product.is_valid(world) ||
      product.tile_count_ != detail::tile_count<World>() ||
      product.chunk_count_ != World::chunk_count ||
      product.local_tile_count_ != World::local_tile_count ||
      product.shape_size_ != ShapeTraits<Shape>::size ||
      product.chunk_extent_ != ShapeTraits<Shape>::chunk) {
    return PathResult{PathStatus::NoPath, 0, 0, 0, scratch.path_};
  }

  const auto start_index = detail::tile_index<Shape>(start);
  auto current = start_index;
  auto current_distance = product.distance_[static_cast<std::size_t>(current)];
  if (current_distance == infinite_distance) {
    return PathResult{PathStatus::NoPath, 0, 0, product.reached_nodes_,
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
          const auto neighbor_distance =
              product.distance_[static_cast<std::size_t>(neighbor_index)];
          if (neighbor_distance < next_distance) {
            next = neighbor_index;
            next_distance = neighbor_distance;
          }
        });

    if (next == current || next_distance + 1 != current_distance) {
      scratch.path_.clear();
      return PathResult{PathStatus::NoPath, 0, 0, product.reached_nodes_,
                        scratch.path_};
    }

    current = next;
    current_distance = product.distance_[static_cast<std::size_t>(current)];
    scratch.path_.push_back(detail::tile_coord<Shape>(current));
    TESS_DIAG_EVENT(path_reconstruct_node);
  }

  return PathResult{PathStatus::Found,
                    product.distance_[static_cast<std::size_t>(start_index)],
                    scratch.path_.size(), product.reached_nodes_,
                    scratch.path_};
}

template <typename World, typename Tag>
auto nearest_target(const World& world, Coord3 start,
                    const DistanceFieldProduct& product,
                    DistanceFieldScratch& scratch) -> NearestTargetResult {
  // Consumes a dense-only distance-field product; guarded directly rather than
  // only transitively through distance_field_product_path below.
  static_assert(
      std::is_same_v<typename World::residency_type, AlwaysResident>,
      "nearest_target is dense-only; the sparse distance-field product slice "
      "lands later.");
  const auto path =
      distance_field_product_path<World, Tag>(world, start, product, scratch);
  auto target = Coord3{};
  if (path.status == PathStatus::Found && !path.path.empty()) {
    target = path.path.back();
  }
  return NearestTargetResult{
      path.status,         path.cost,          target,
      path.expanded_nodes, path.reached_nodes, path.path,
  };
}

}  // namespace tess
