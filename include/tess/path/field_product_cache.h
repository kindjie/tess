#pragma once

#include <tess/core/tag_identity.h>
#include <tess/path/path.h>

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace tess {

/// Owns an ordered set of goals used to build a reusable distance product.
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

/// Reports the closest reachable goal and a scratch-owned path to it.
struct NearestTargetResult {
  PathStatus status = PathStatus::NoPath;
  std::uint32_t cost = 0;
  Coord3 target{};
  std::size_t expanded_nodes = 0;
  std::size_t reached_nodes = 0;
  std::span<const Coord3> path;
  std::uint32_t cost_scale = 1;
};

/// Owns a reusable dense-world multi-goal distance field and dependencies.
///
/// Building may allocate; reserve goals, nodes, and dependencies for a warm
/// allocation-free rebuild. A product is invalid after relevant chunk edits.
class DistanceFieldProduct {
 public:
  void reserve_goals(std::size_t count) { goals_.reserve(count); }

  void reserve_nodes(std::size_t node_count) { distance_.reserve(node_count); }

  // Dependency sets are bounded by the world's chunk count (failure
  // products capture every chunk): reserve chunk_count to keep steady-state
  // rebuilds allocation-free.
  void reserve_dependencies(std::size_t count) { dependencies_.reserve(count); }

  void clear() noexcept {
    status_ = PathStatus::NoPath;
    expanded_nodes_ = 0;
    reached_nodes_ = 0;
    tile_count_ = 0;
    chunk_count_ = 0;
    local_tile_count_ = 0;
    model_class_identity_ = 0;
    model_lattice_identity_ = 0;
    model_lattice_version_ = 0;
    model_step_identity_ = 0;
    model_cost_scale_ = 0;
    model_provider_identity_ = 0;
    model_provider_revision_ = 0;
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
  friend class FieldProductCache;

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
  std::uintptr_t model_class_identity_ = 0;
  std::uint32_t model_lattice_identity_ = 0;
  std::uint32_t model_lattice_version_ = 0;
  std::uint32_t model_step_identity_ = 0;
  std::uint32_t model_cost_scale_ = 0;
  std::uintptr_t model_provider_identity_ = 0;
  std::uint64_t model_provider_revision_ = 0;
  std::vector<Coord3> goals_;
  std::vector<std::uint32_t> distance_;
  ChunkVersionDependencies dependencies_;
};

/// Summarizes field-product cache residency and lookup outcomes.
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
/// Owns LRU-cached distance-field products within a configurable byte budget.
///
/// Entries are keyed by field type, shape, goals, and exact provider revision,
/// but not by world identity; use one cache per world. Historical provider
/// revisions remain distinct entries until normal LRU eviction, so configure a
/// finite byte budget when provider state changes without bound. Lookup
/// pointers remain cache-owned and are invalidated when their entry is
/// replaced, evicted, or cleared.
class FieldProductCache {
  struct Key {
    std::uintptr_t movement_class = 0;
    std::size_t tile_count = 0;
    std::uint64_t chunk_count = 0;
    std::uint64_t local_tile_count = 0;
    Extent3 shape_size{};
    Extent3 chunk_extent{};
    std::uint32_t lattice_identity = 0;
    std::uint32_t lattice_version = 0;
    std::uint32_t step_identity = 0;
    std::uint32_t cost_scale = 0;
    std::uintptr_t provider_identity = 0;
    std::uint64_t provider_revision = 0;
    std::vector<Coord3> goals;
  };

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
    return lookup<World, Tag, AdjacentTransitions>(world, goals,
                                                   AdjacentTransitions{});
  }

  /// Looks up a product for the exact provider type and revision.
  template <typename World, typename Tag, typename Provider>
  [[nodiscard]] auto lookup(const World& world, const GoalSet& goals,
                            const Provider& provider)
      -> const DistanceFieldProduct* {
    for (std::size_t i = 0; i < entries_.size(); ++i) {
      auto& entry = entries_[i];
      if (!key_matches<World, Tag, Provider>(entry.key, goals.goals(),
                                             provider)) {
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

  /// Looks up a weighted product for the exact movement class and provider.
  template <typename World, typename Class, typename Provider>
  [[nodiscard]] auto lookup_weighted(const World& world, const GoalSet& goals,
                                     const Provider& provider)
      -> const DistanceFieldProduct* {
    static_assert(std::derived_from<Class, movement::movement_class_tag>);
    for (std::size_t i = 0; i < entries_.size(); ++i) {
      auto& entry = entries_[i];
      if (!key_matches_class<World, Class, Provider>(entry.key, goals.goals(),
                                                     provider)) {
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

  /// Looks up a weighted product using regular adjacent transitions.
  template <typename World, typename Class>
  [[nodiscard]] auto lookup_weighted(const World& world, const GoalSet& goals)
      -> const DistanceFieldProduct* {
    return lookup_weighted<World, Class, AdjacentTransitions>(
        world, goals, AdjacentTransitions{});
  }

  // Takes ownership of `product` by move; world-sized field data is never
  // copied. The argument is left moved-from (empty but reusable). A product
  // whose entry exceeds the byte budget cannot be cached at all; that store
  // clears the entire cache and returns false.
  template <typename World, typename Tag>
  auto store(DistanceFieldProduct&& product) -> bool {
    return store<World, Tag, AdjacentTransitions>(std::move(product),
                                                  AdjacentTransitions{});
  }

  /// Stores a product under the exact provider type and revision.
  template <typename World, typename Tag, typename Provider>
  auto store(DistanceFieldProduct&& product, const Provider& provider) -> bool {
    if (product.status() != PathStatus::Found) {
      return false;
    }

    auto key = make_key<World, Tag, Provider>(product.goals(), provider);
    if (product.model_class_identity_ != key.movement_class ||
        product.tile_count_ != key.tile_count ||
        product.chunk_count_ != key.chunk_count ||
        product.local_tile_count_ != key.local_tile_count ||
        product.model_lattice_identity_ != key.lattice_identity ||
        product.model_lattice_version_ != key.lattice_version ||
        product.model_step_identity_ != key.step_identity ||
        product.model_cost_scale_ != key.cost_scale ||
        product.model_provider_identity_ != key.provider_identity ||
        product.model_provider_revision_ != key.provider_revision) {
      return false;
    }
    return store_with_key(std::move(product), std::move(key));
  }

  /// Stores a weighted product under its movement class and provider.
  template <typename World, typename Class, typename Provider>
  auto store_weighted(DistanceFieldProduct&& product, const Provider& provider)
      -> bool {
    static_assert(std::derived_from<Class, movement::movement_class_tag>);
    if (product.status() != PathStatus::Found) {
      return false;
    }
    auto key =
        make_key_for_class<World, Class, Provider>(product.goals(), provider);
    if (product.model_class_identity_ != key.movement_class ||
        product.tile_count_ != key.tile_count ||
        product.chunk_count_ != key.chunk_count ||
        product.local_tile_count_ != key.local_tile_count ||
        product.model_lattice_identity_ != key.lattice_identity ||
        product.model_lattice_version_ != key.lattice_version ||
        product.model_step_identity_ != key.step_identity ||
        product.model_cost_scale_ != key.cost_scale ||
        product.model_provider_identity_ != key.provider_identity ||
        product.model_provider_revision_ != key.provider_revision) {
      return false;
    }
    return store_with_key(std::move(product), std::move(key));
  }

  /// Stores a weighted product using regular adjacent transitions.
  template <typename World, typename Class>
  auto store_weighted(DistanceFieldProduct&& product) -> bool {
    return store_weighted<World, Class, AdjacentTransitions>(
        std::move(product), AdjacentTransitions{});
  }

 private:
  auto store_with_key(DistanceFieldProduct&& product, Key key) -> bool {
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

    auto owned_product =
        std::make_unique<DistanceFieldProduct>(std::move(product));
    auto candidate =
        Entry{std::move(key), std::move(owned_product), clock_ + 1u, bytes};
    // Construct every throwing component before this insertion commit. If
    // allocating either the product or vector storage fails, no null/partial
    // entry becomes visible to lookup and all cache statistics stay unchanged.
    entries_.push_back(std::move(candidate));
    ++clock_;
    bytes_ += bytes;
    evict_to_budget();
    return true;
  }
  // Each product lives behind a `unique_ptr` so `entries_` growth and
  // eviction of other entries never relocate a product that a caller still
  // borrows through `lookup()`.
  struct Entry {
    Key key;
    std::unique_ptr<DistanceFieldProduct> product;
    std::uint64_t last_used = 0;
    std::size_t bytes = 0;
  };

  template <typename World, typename Tag, typename Provider>
  [[nodiscard]] static auto make_key(std::span<const Coord3> goals,
                                     const Provider& provider) -> Key {
    using Class = movement::movement_class_of<Tag>;
    using UnitClass = movement::detail::UnitMovementClass<Class>;
    return make_key_for_class<World, UnitClass, Provider>(goals, provider);
  }

  template <typename World, typename Class, typename Provider>
  [[nodiscard]] static auto make_key_for_class(std::span<const Coord3> goals,
                                               const Provider& provider)
      -> Key {
    using Model = ResolvedTransitionModel<World, Class, Provider>;
    const auto model = Model{provider};
    return Key{
        detail::tag_identity<typename Model::class_type>(),
        detail::tile_count<World>(),
        World::chunk_count,
        World::local_tile_count,
        ShapeTraits<typename World::shape_type>::size,
        ShapeTraits<typename World::shape_type>::chunk,
        static_cast<std::uint32_t>(Model::lattice_identity),
        Model::lattice_version,
        static_cast<std::uint32_t>(Model::step_policy_identity),
        Model::cost_scale,
        detail::tag_identity<Provider>(),
        model.revision(),
        std::vector<Coord3>{goals.begin(), goals.end()},
    };
  }

  [[nodiscard]] static auto keys_equal(const Key& lhs, const Key& rhs) noexcept
      -> bool {
    return lhs.movement_class == rhs.movement_class &&
           lhs.tile_count == rhs.tile_count &&
           lhs.chunk_count == rhs.chunk_count &&
           lhs.local_tile_count == rhs.local_tile_count &&
           lhs.shape_size == rhs.shape_size &&
           lhs.chunk_extent == rhs.chunk_extent &&
           lhs.lattice_identity == rhs.lattice_identity &&
           lhs.lattice_version == rhs.lattice_version &&
           lhs.step_identity == rhs.step_identity &&
           lhs.cost_scale == rhs.cost_scale &&
           lhs.provider_identity == rhs.provider_identity &&
           lhs.provider_revision == rhs.provider_revision &&
           lhs.goals == rhs.goals;
  }

  template <typename World, typename Tag, typename Provider>
  [[nodiscard]] static auto key_matches(const Key& key,
                                        std::span<const Coord3> goals,
                                        const Provider& provider) noexcept
      -> bool {
    using Class = movement::movement_class_of<Tag>;
    using UnitClass = movement::detail::UnitMovementClass<Class>;
    return key_matches_class<World, UnitClass, Provider>(key, goals, provider);
  }

  template <typename World, typename Class, typename Provider>
  [[nodiscard]] static auto key_matches_class(const Key& key,
                                              std::span<const Coord3> goals,
                                              const Provider& provider) noexcept
      -> bool {
    using Model = ResolvedTransitionModel<World, Class, Provider>;
    const auto model = Model{provider};
    return key.movement_class ==
               detail::tag_identity<typename Model::class_type>() &&
           key.tile_count == detail::tile_count<World>() &&
           key.chunk_count == World::chunk_count &&
           key.local_tile_count == World::local_tile_count &&
           key.shape_size == ShapeTraits<typename World::shape_type>::size &&
           key.chunk_extent == ShapeTraits<typename World::shape_type>::chunk &&
           key.lattice_identity ==
               static_cast<std::uint32_t>(Model::lattice_identity) &&
           key.lattice_version == Model::lattice_version &&
           key.step_identity ==
               static_cast<std::uint32_t>(Model::step_policy_identity) &&
           key.cost_scale == Model::cost_scale &&
           key.provider_identity == detail::tag_identity<Provider>() &&
           key.provider_revision == model.revision() &&
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

/// Builds a dense-world multi-goal field into caller-owned `product`.
///
/// Invalid goals return `InvalidGoal`. Reusing reserved product and scratch
/// capacity avoids steady-state allocation.
template <typename World, typename Tag, typename Provider>
auto build_distance_field_product(const World& world, const GoalSet& goals,
                                  DistanceFieldScratch& scratch,
                                  DistanceFieldProduct& product,
                                  const Provider& provider)
    -> DistanceFieldResult {
  using Shape = typename World::shape_type;
  using Class = movement::movement_class_of<Tag>;
  using UnitClass = movement::detail::UnitMovementClass<Class>;
  using Model = ResolvedTransitionModel<World, UnitClass, Provider>;
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
  const auto model = Model{provider};
  product.goals_.assign(goals.goals().begin(), goals.goals().end());
  product.tile_count_ = detail::tile_count<World>();
  product.chunk_count_ = World::chunk_count;
  product.local_tile_count_ = World::local_tile_count;
  product.shape_size_ = ShapeTraits<Shape>::size;
  product.chunk_extent_ = ShapeTraits<Shape>::chunk;
  product.model_class_identity_ =
      detail::tag_identity<typename Model::class_type>();
  product.model_lattice_identity_ =
      static_cast<std::uint32_t>(Model::lattice_identity);
  product.model_lattice_version_ = Model::lattice_version;
  product.model_step_identity_ =
      static_cast<std::uint32_t>(Model::step_policy_identity);
  product.model_cost_scale_ = Model::cost_scale;
  product.model_provider_identity_ = detail::tag_identity<Provider>();
  product.model_provider_revision_ = model.revision();

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
    if constexpr (Model::cost_scale == 1 && !Model::has_special_transitions) {
      scratch.frontier_.push_back(goal_index);
      TESS_DIAG_EVENT(path_heap_push);
    } else {
      scratch.weighted_frontier_.push_back(
          PathScratch::OpenNode{goal_index, 0, 0});
      std::push_heap(scratch.weighted_frontier_.begin(),
                     scratch.weighted_frontier_.end(), detail::open_node_less);
      TESS_DIAG_EVENT(path_heap_push);
    }
  }

  std::size_t expanded_nodes = 0;
  auto cost_overflow = false;
  if constexpr (Model::cost_scale == 1 && !Model::has_special_transitions) {
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
      const auto visit_neighbor = [&](std::uint64_t neighbor_index) {
        const auto neighbor_offset = static_cast<std::size_t>(neighbor_index);
        if (scratch.is_current(neighbor_offset)) {
          TESS_DIAG_EVENT(path_neighbor_closed);
          return;
        }

        scratch.distance_[neighbor_offset] = current_distance + 1;
        scratch.touch_node(neighbor_index);
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
              TESS_DIAG_EVENT(path_passability_check);
              if (!detail::is_passable_index<World, Tag>(world,
                                                         neighbor_index)) {
                TESS_DIAG_EVENT(path_neighbor_blocked);
                return;
              }
              visit_neighbor(neighbor_index);
            });
      } else {
        model.for_each_reverse(world, current_coord, current, [&](auto probe) {
          TESS_DIAG_EVENT(path_neighbor_candidate);
          if (probe.availability == TransitionAvailability::Legal) {
            visit_neighbor(probe.to_index);
          }
        });
      }
    }
  } else {
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
      const auto current_coord = detail::tile_coord<Shape>(current.index);
      model.for_each_reverse(
          world, current_coord, current.index, [&](auto probe) {
            TESS_DIAG_EVENT(path_neighbor_candidate);
            if (probe.availability != TransitionAvailability::Legal) {
              return;
            }
            if (probe.cost_overflow) {
              cost_overflow = true;
              return;
            }
            const auto neighbor_offset =
                static_cast<std::size_t>(probe.to_index);
            TESS_DIAG_EVENT(path_relax_attempt);
            if (!scratch.is_current(neighbor_offset)) {
              scratch.distance_[neighbor_offset] = infinite_distance;
              scratch.touch_node(probe.to_index);
              TESS_DIAG_EVENT(path_touch_node);
            }
            const auto next_distance =
                detail::saturating_add(current_distance, probe.cost);
            if (next_distance == infinite_distance) {
              cost_overflow = true;
              return;
            }
            if (next_distance < scratch.distance_[neighbor_offset]) {
              TESS_DIAG_EVENT(path_relax_success);
              scratch.distance_[neighbor_offset] = next_distance;
              scratch.weighted_frontier_.push_back(PathScratch::OpenNode{
                  probe.to_index, next_distance, next_distance});
              std::push_heap(scratch.weighted_frontier_.begin(),
                             scratch.weighted_frontier_.end(),
                             detail::open_node_less);
              TESS_DIAG_EVENT(path_heap_push);
            }
          });
    }
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
  // Every candidate transition and its clearance tiles are dependencies, so
  // opening a blocked frontier or diagonal corner invalidates the product.
  // The scratch seen-set keeps this pass linear (add_chunk's
  // duplicate scan would make it quadratic in chunk count), and indexing
  // re-reads chunks() each pass because appends may grow the vector.
  {
    auto& seen = scratch.chunk_seen_;
    seen.assign(static_cast<std::size_t>(World::chunk_count), 0);
    for (const auto dependency : product.dependencies_.chunks()) {
      seen[static_cast<std::size_t>(dependency.key.value)] = 1;
    }
    for (const auto index : scratch.touched_) {
      model.for_each_dependency_chunk(
          world, detail::tile_coord<Shape>(index), [&](ChunkKey key) {
            auto& mark = seen[static_cast<std::size_t>(key.value)];
            if (mark == 0) {
              mark = 1;
              product.dependencies_.add_chunk_unique(world, key);
            }
          });
    }
  }
  if constexpr (Model::has_special_transitions) {
    // A provider may derive an edge from state outside the regular stencil.
    // Without a generic provider index, whole-world capture is the only
    // sound invalidation rule for persistent dense products.
    product.dependencies_.capture_all(world);
  }
  product.status_ =
      cost_overflow ? PathStatus::CostOverflow : PathStatus::Found;
  product.expanded_nodes_ = expanded_nodes;
  product.reached_nodes_ = scratch.touched_.size();

  return DistanceFieldResult{product.status_, product.expanded_nodes_,
                             product.reached_nodes_};
}

template <typename World, typename Tag>
auto build_distance_field_product(const World& world, const GoalSet& goals,
                                  DistanceFieldScratch& scratch,
                                  DistanceFieldProduct& product)
    -> DistanceFieldResult {
  return build_distance_field_product<World, Tag, AdjacentTransitions>(
      world, goals, scratch, product, AdjacentTransitions{});
}

/// Builds a dense multi-goal weighted field into a reusable product.
template <typename World, typename Class, typename Provider>
auto build_weighted_distance_field_product(const World& world,
                                           const GoalSet& goals,
                                           DistanceFieldScratch& scratch,
                                           DistanceFieldProduct& product,
                                           const Provider& provider)
    -> DistanceFieldResult {
  static_assert(std::derived_from<Class, movement::movement_class_tag>);
  static_assert(std::is_same_v<typename World::residency_type, AlwaysResident>,
                "weighted distance-field products are dense-only");
  using Shape = typename World::shape_type;
  using Model = ResolvedTransitionModel<World, Class, Provider>;
  constexpr auto infinite_distance = std::numeric_limits<std::uint32_t>::max();

  scratch.clear_build();
  product.clear();
  const auto model = Model{provider};
  product.goals_.assign(goals.goals().begin(), goals.goals().end());
  product.tile_count_ = detail::tile_count<World>();
  product.chunk_count_ = World::chunk_count;
  product.local_tile_count_ = World::local_tile_count;
  product.shape_size_ = ShapeTraits<Shape>::size;
  product.chunk_extent_ = ShapeTraits<Shape>::chunk;
  product.model_class_identity_ = detail::tag_identity<Class>();
  product.model_lattice_identity_ =
      static_cast<std::uint32_t>(Model::lattice_identity);
  product.model_lattice_version_ = Model::lattice_version;
  product.model_step_identity_ =
      static_cast<std::uint32_t>(Model::step_policy_identity);
  product.model_cost_scale_ = Model::cost_scale;
  product.model_provider_identity_ = detail::tag_identity<Provider>();
  product.model_provider_revision_ = model.revision();

  if (goals.empty()) {
    return {PathStatus::InvalidGoal, 0, 0};
  }
  for (const auto goal : goals.goals()) {
    if (!contains<Shape>(goal) ||
        !detail::is_passable<World, Class>(world, goal)) {
      return {PathStatus::InvalidGoal, 0, 0};
    }
    const auto index = detail::tile_index<Shape>(goal);
    if (detail::tile_entry_cost_index<World, Class>(world, index) == 0) {
      return {PathStatus::InvalidGoal, 0, 0};
    }
  }

  const auto node_count = detail::tile_count<World>();
  if (scratch.distance_.size() != node_count) {
    scratch.generation_.assign(node_count, 0);
    scratch.distance_.assign(node_count, infinite_distance);
  }
  for (const auto goal : goals.goals()) {
    const auto index = detail::tile_index<Shape>(goal);
    const auto offset = static_cast<std::size_t>(index);
    if (scratch.is_current(offset)) {
      continue;
    }
    scratch.distance_[offset] = 0;
    scratch.touch_node(index);
    scratch.weighted_frontier_.push_back(PathScratch::OpenNode{index, 0, 0});
    std::push_heap(scratch.weighted_frontier_.begin(),
                   scratch.weighted_frontier_.end(), detail::open_node_less);
  }

  auto expanded_nodes = std::size_t{0};
  auto cost_overflow = false;
  while (!scratch.weighted_frontier_.empty()) {
    std::pop_heap(scratch.weighted_frontier_.begin(),
                  scratch.weighted_frontier_.end(), detail::open_node_less);
    const auto current = scratch.weighted_frontier_.back();
    scratch.weighted_frontier_.pop_back();
    const auto current_offset = static_cast<std::size_t>(current.index);
    const auto current_distance =
        scratch.distance_at(current_offset, infinite_distance);
    if (current.g != current_distance) {
      continue;
    }
    ++expanded_nodes;
    model.for_each_reverse(
        world, detail::tile_coord<Shape>(current.index), current.index,
        [&](auto probe) {
          if (probe.availability != TransitionAvailability::Legal) {
            return;
          }
          if (probe.cost_overflow) {
            cost_overflow = true;
            return;
          }
          const auto offset = static_cast<std::size_t>(probe.to_index);
          if (!scratch.is_current(offset)) {
            scratch.distance_[offset] = infinite_distance;
            scratch.touch_node(probe.to_index);
          }
          const auto next =
              detail::saturating_add(current_distance, probe.cost);
          if (next == infinite_distance) {
            cost_overflow = true;
            return;
          }
          if (next < scratch.distance_[offset]) {
            scratch.distance_[offset] = next;
            scratch.weighted_frontier_.push_back(
                PathScratch::OpenNode{probe.to_index, next, next});
            std::push_heap(scratch.weighted_frontier_.begin(),
                           scratch.weighted_frontier_.end(),
                           detail::open_node_less);
          }
        });
  }

  product.distance_.assign(node_count, infinite_distance);
  for (const auto index : scratch.touched_) {
    const auto offset = static_cast<std::size_t>(index);
    product.distance_[offset] = scratch.distance_[offset];
    product.dependencies_.add_chunk(
        world,
        chunk_key<Shape>(tile_key<Shape>(detail::tile_coord<Shape>(index))));
  }
  auto& seen = scratch.chunk_seen_;
  seen.assign(static_cast<std::size_t>(World::chunk_count), 0);
  for (const auto dependency : product.dependencies_.chunks()) {
    seen[static_cast<std::size_t>(dependency.key.value)] = 1;
  }
  for (const auto index : scratch.touched_) {
    model.for_each_dependency_chunk(
        world, detail::tile_coord<Shape>(index), [&](ChunkKey key) {
          auto& mark = seen[static_cast<std::size_t>(key.value)];
          if (mark == 0) {
            mark = 1;
            product.dependencies_.add_chunk_unique(world, key);
          }
        });
  }
  if constexpr (Model::has_special_transitions) {
    product.dependencies_.capture_all(world);
  }
  product.status_ =
      cost_overflow ? PathStatus::CostOverflow : PathStatus::Found;
  product.expanded_nodes_ = expanded_nodes;
  product.reached_nodes_ = scratch.touched_.size();
  return {product.status_, product.expanded_nodes_, product.reached_nodes_};
}

/// Builds a weighted product using regular adjacent transitions.
template <typename World, typename Class>
auto build_weighted_distance_field_product(const World& world,
                                           const GoalSet& goals,
                                           DistanceFieldScratch& scratch,
                                           DistanceFieldProduct& product)
    -> DistanceFieldResult {
  return build_weighted_distance_field_product<World, Class,
                                               AdjacentTransitions>(
      world, goals, scratch, product, AdjacentTransitions{});
}

/// Reconstructs a path from `start` through a valid dense-world product.
///
/// The returned path borrows `scratch` storage until its next mutation.
template <typename World, typename Tag, typename Provider>
auto distance_field_product_path(const World& world, Coord3 start,
                                 const DistanceFieldProduct& product,
                                 DistanceFieldScratch& scratch,
                                 const Provider& provider) -> PathResult {
  using Shape = typename World::shape_type;
  using Class = movement::movement_class_of<Tag>;
  using UnitClass = movement::detail::UnitMovementClass<Class>;
  using Model = ResolvedTransitionModel<World, UnitClass, Provider>;
  const auto model = Model{provider};
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
      product.chunk_extent_ != ShapeTraits<Shape>::chunk ||
      product.model_class_identity_ !=
          detail::tag_identity<typename Model::class_type>() ||
      product.model_lattice_identity_ !=
          static_cast<std::uint32_t>(Model::lattice_identity) ||
      product.model_lattice_version_ != Model::lattice_version ||
      product.model_step_identity_ !=
          static_cast<std::uint32_t>(Model::step_policy_identity) ||
      product.model_cost_scale_ != Model::cost_scale ||
      product.model_provider_identity_ != detail::tag_identity<Provider>() ||
      product.model_provider_revision_ != model.revision()) {
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
    if constexpr (Model::preserves_default_connectivity &&
                  std::is_same_v<typename Model::step_policy,
                                 movement::DefaultSteps>) {
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
    } else {
      auto next_cost = std::uint32_t{0};
      model.for_each_forward(world, current_coord, current, [&](auto probe) {
        if (probe.availability != TransitionAvailability::Legal ||
            probe.cost_overflow) {
          return;
        }
        const auto neighbor_distance =
            product.distance_[static_cast<std::size_t>(probe.to_index)];
        if (neighbor_distance < next_distance &&
            detail::saturating_add(neighbor_distance, probe.cost) ==
                current_distance) {
          next = probe.to_index;
          next_distance = neighbor_distance;
          next_cost = probe.cost;
        }
      });

      if (next == current || detail::saturating_add(next_distance, next_cost) !=
                                 current_distance) {
        scratch.path_.clear();
        return PathResult{PathStatus::NoPath, 0, 0, product.reached_nodes_,
                          scratch.path_};
      }
    }

    current = next;
    current_distance = product.distance_[static_cast<std::size_t>(current)];
    scratch.path_.push_back(detail::tile_coord<Shape>(current));
    TESS_DIAG_EVENT(path_reconstruct_node);
  }

  return PathResult{PathStatus::Found,
                    product.distance_[static_cast<std::size_t>(start_index)],
                    scratch.path_.size(),
                    product.reached_nodes_,
                    scratch.path_,
                    Model::cost_scale};
}

template <typename World, typename Tag>
auto distance_field_product_path(const World& world, Coord3 start,
                                 const DistanceFieldProduct& product,
                                 DistanceFieldScratch& scratch) -> PathResult {
  return distance_field_product_path<World, Tag, AdjacentTransitions>(
      world, start, product, scratch, AdjacentTransitions{});
}

/// Reconstructs an exact weighted path through a valid reusable product.
template <typename World, typename Class, typename Provider>
auto weighted_distance_field_product_path(const World& world, Coord3 start,
                                          const DistanceFieldProduct& product,
                                          DistanceFieldScratch& scratch,
                                          const Provider& provider)
    -> PathResult {
  static_assert(std::derived_from<Class, movement::movement_class_tag>);
  static_assert(std::is_same_v<typename World::residency_type, AlwaysResident>,
                "weighted distance-field products are dense-only");
  using Shape = typename World::shape_type;
  using Model = ResolvedTransitionModel<World, Class, Provider>;
  constexpr auto infinite_distance = std::numeric_limits<std::uint32_t>::max();
  const auto model = Model{provider};

  scratch.clear_path();
  if (!contains<Shape>(start) ||
      !detail::is_passable<World, Class>(world, start)) {
    return {PathStatus::InvalidStart, 0, 0, 0, scratch.path_};
  }
  const auto start_index = detail::tile_index<Shape>(start);
  // LegacyWeighted intentionally keeps boolean passability independent from
  // its cost field, but a normalized zero entry cost is still impassable to
  // every weighted query. Check both before a cached distance can be replayed.
  if (detail::tile_entry_cost_index<World, Class>(world, start_index) == 0) {
    return {PathStatus::InvalidStart, 0, 0, 0, scratch.path_};
  }
  if (product.status_ != PathStatus::Found || !product.is_valid(world) ||
      product.tile_count_ != detail::tile_count<World>() ||
      product.chunk_count_ != World::chunk_count ||
      product.local_tile_count_ != World::local_tile_count ||
      product.shape_size_ != ShapeTraits<Shape>::size ||
      product.chunk_extent_ != ShapeTraits<Shape>::chunk ||
      product.model_class_identity_ != detail::tag_identity<Class>() ||
      product.model_lattice_identity_ !=
          static_cast<std::uint32_t>(Model::lattice_identity) ||
      product.model_lattice_version_ != Model::lattice_version ||
      product.model_step_identity_ !=
          static_cast<std::uint32_t>(Model::step_policy_identity) ||
      product.model_cost_scale_ != Model::cost_scale ||
      product.model_provider_identity_ != detail::tag_identity<Provider>() ||
      product.model_provider_revision_ != model.revision()) {
    return {PathStatus::NoPath, 0, 0, 0, scratch.path_};
  }

  auto current = start_index;
  auto current_distance = product.distance_[start_index];
  if (current_distance == infinite_distance) {
    return {PathStatus::NoPath, 0, 0, product.reached_nodes_, scratch.path_};
  }
  scratch.path_.push_back(start);
  while (current_distance > 0) {
    auto next = current;
    auto next_distance = current_distance;
    auto next_cost = std::uint32_t{0};
    model.for_each_forward(
        world, detail::tile_coord<Shape>(current), current, [&](auto probe) {
          if (probe.availability != TransitionAvailability::Legal ||
              probe.cost_overflow) {
            return;
          }
          const auto candidate = product.distance_[probe.to_index];
          if (candidate < next_distance &&
              detail::saturating_add(candidate, probe.cost) ==
                  current_distance) {
            next = probe.to_index;
            next_distance = candidate;
            next_cost = probe.cost;
          }
        });
    if (next == current ||
        detail::saturating_add(next_distance, next_cost) != current_distance) {
      scratch.path_.clear();
      return {PathStatus::NoPath, 0, 0, product.reached_nodes_, scratch.path_};
    }
    current = next;
    current_distance = product.distance_[current];
    scratch.path_.push_back(detail::tile_coord<Shape>(current));
  }

  return {PathStatus::Found,    product.distance_[start_index],
          scratch.path_.size(), product.reached_nodes_,
          scratch.path_,        Model::cost_scale};
}

/// Reconstructs a weighted product path using regular adjacent transitions.
template <typename World, typename Class>
auto weighted_distance_field_product_path(const World& world, Coord3 start,
                                          const DistanceFieldProduct& product,
                                          DistanceFieldScratch& scratch)
    -> PathResult {
  return weighted_distance_field_product_path<World, Class,
                                              AdjacentTransitions>(
      world, start, product, scratch, AdjacentTransitions{});
}

/// Finds the closest reachable goal represented by a valid dense product.
///
/// The returned path borrows `scratch` storage until its next mutation.
template <typename World, typename Tag, typename Provider>
auto nearest_target(const World& world, Coord3 start,
                    const DistanceFieldProduct& product,
                    DistanceFieldScratch& scratch, const Provider& provider)
    -> NearestTargetResult {
  // Consumes a dense-only distance-field product; guarded directly rather than
  // only transitively through distance_field_product_path below.
  static_assert(
      std::is_same_v<typename World::residency_type, AlwaysResident>,
      "nearest_target is dense-only; the sparse distance-field product slice "
      "lands later.");
  const auto path = distance_field_product_path<World, Tag, Provider>(
      world, start, product, scratch, provider);
  auto target = Coord3{};
  if (path.status == PathStatus::Found && !path.path.empty()) {
    target = path.path.back();
  }
  return NearestTargetResult{
      path.status,        path.cost, target,          path.expanded_nodes,
      path.reached_nodes, path.path, path.cost_scale,
  };
}

template <typename World, typename Tag>
auto nearest_target(const World& world, Coord3 start,
                    const DistanceFieldProduct& product,
                    DistanceFieldScratch& scratch) -> NearestTargetResult {
  return nearest_target<World, Tag, AdjacentTransitions>(
      world, start, product, scratch, AdjacentTransitions{});
}

/// Finds the lowest-cost goal represented by a weighted product.
template <typename World, typename Class, typename Provider>
auto weighted_nearest_target(const World& world, Coord3 start,
                             const DistanceFieldProduct& product,
                             DistanceFieldScratch& scratch,
                             const Provider& provider) -> NearestTargetResult {
  const auto path = weighted_distance_field_product_path<World, Class>(
      world, start, product, scratch, provider);
  const auto target = path.status == PathStatus::Found && !path.path.empty()
                          ? path.path.back()
                          : Coord3{};
  return {path.status,        path.cost, target,         path.expanded_nodes,
          path.reached_nodes, path.path, path.cost_scale};
}

/// Finds the lowest-cost weighted goal using regular adjacent transitions.
template <typename World, typename Class>
auto weighted_nearest_target(const World& world, Coord3 start,
                             const DistanceFieldProduct& product,
                             DistanceFieldScratch& scratch)
    -> NearestTargetResult {
  return weighted_nearest_target<World, Class, AdjacentTransitions>(
      world, start, product, scratch, AdjacentTransitions{});
}

}  // namespace tess
