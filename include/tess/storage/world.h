#pragma once

#include <tess/core/assert.h>
#include <tess/core/shape.h>
#include <tess/storage/chunk_meta.h>
#include <tess/storage/chunk_page.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <vector>

namespace tess {

/** Selects the dense world that materializes every chunk at construction. */
struct AlwaysResident {};

/** Primary template for a world selected by its residency policy. */
template <typename Shape, typename Schema, typename Residency>
class World;

/**
 * Dense owner of every chunk page and its metadata.
 *
 * Construction allocates and value-initializes storage for the complete bounded
 * shape. No operation changes the number of pages afterward, so page pointers,
 * references, and spans remain valid until the world is moved from or
 * destroyed. Query and mutation hot paths do not allocate; methods returning a
 * `std::vector` may. Instances are not internally synchronized: concurrent
 * reads are safe, while mutation requires external synchronization with all
 * accesses to the affected world.
 */
template <typename Shape, typename Schema>
class World<Shape, Schema, AlwaysResident> {
 public:
  using shape_type = Shape;
  using schema_type = Schema;
  using residency_type = AlwaysResident;
  using page_type = ChunkPage<Shape, Schema>;

  static constexpr std::uint64_t chunk_count = ShapeTraits<Shape>::chunk_count;
  static constexpr std::uint64_t local_tile_count =
      ShapeTraits<Shape>::local_tile_count;
  static constexpr std::size_t field_count = Schema::field_count;
  // Tess-owned inline page storage; field-managed dynamic or referenced
  // storage is outside these byte counts.
  static constexpr std::size_t page_byte_size = page_type::byte_size;
  static constexpr std::size_t storage_byte_size =
      static_cast<std::size_t>(chunk_count) * page_byte_size;

  static_assert(chunk_count <= static_cast<std::uint64_t>(
                                   std::numeric_limits<std::size_t>::max()),
                "AlwaysResident World chunk count must fit std::size_t.");
  static_assert(page_byte_size <= std::numeric_limits<std::size_t>::max() /
                                      static_cast<std::size_t>(chunk_count),
                "AlwaysResident World storage bytes must fit std::size_t.");

  /** Allocates and value-initializes all chunk pages and metadata. */
  World() {
    pages_.reserve(static_cast<std::size_t>(chunk_count));
    metadata_.reserve(static_cast<std::size_t>(chunk_count));
    for (std::uint64_t key = 0; key < chunk_count; ++key) {
      const auto chunk = ChunkKey{key};
      pages_.emplace_back(chunk, chunk_coord<Shape>(chunk));
      metadata_.emplace_back();
    }
    dirty_masks_.assign(static_cast<std::size_t>(chunk_count), DirtyMask{});
    active_masks_.assign(static_cast<std::size_t>(chunk_count), ActiveMask{});
    dirty_bounds_.assign(static_cast<std::size_t>(chunk_count), Box3{});
  }

  /** Returns all pages in chunk-key order without allocating. */
  [[nodiscard]] auto chunks() noexcept -> std::span<page_type> {
    return {pages_.data(), pages_.size()};
  }

  /** Const overload of `chunks()` with the same lifetime contract. */
  [[nodiscard]] auto chunks() const noexcept -> std::span<const page_type> {
    return {pages_.data(), pages_.size()};
  }

  /**
   * Assigns `value` to `Tag` on every tile.
   *
   * The traversal and `TileFieldValue` assignment perform no allocation and
   * cannot throw. This is a storage write only: like direct `field()` writes,
   * it does not update dirty/active masks or topology/content versions.
   */
  template <typename Tag>
  void fill_field(const Schema::template value_type<Tag>& value) noexcept {
    for (auto& page : pages_) {
      for (auto& tile : page.template field_span<Tag>()) {
        tile = value;
      }
    }
  }

  /**
   * Returns a page by key without runtime error recovery.
   * @pre `key` is inside the world; debug builds assert this precondition.
   */
  [[nodiscard]] auto chunk(ChunkKey key) noexcept -> page_type& {
    TESS_ASSERT(key.value < chunk_count);
    return pages_[static_cast<std::size_t>(key.value)];
  }

  /** Const overload of `chunk(ChunkKey)` with the same precondition. */
  [[nodiscard]] auto chunk(ChunkKey key) const noexcept -> const page_type& {
    TESS_ASSERT(key.value < chunk_count);
    return pages_[static_cast<std::size_t>(key.value)];
  }

  /**
   * Returns a page by coordinate without runtime error recovery.
   * @pre `coord` is inside the world.
   */
  [[nodiscard]] auto chunk(ChunkCoord3 coord) noexcept -> page_type& {
    return chunk(chunk_key<Shape>(coord));
  }

  /** Const overload of `chunk(ChunkCoord3)` with the same precondition. */
  [[nodiscard]] auto chunk(ChunkCoord3 coord) const noexcept
      -> const page_type& {
    return chunk(chunk_key<Shape>(coord));
  }

  /** Returns a page by key, or null when the key is out of bounds. */
  [[nodiscard]] auto try_chunk(ChunkKey key) noexcept -> page_type* {
    if (key.value >= chunk_count) {
      return nullptr;
    }
    return &chunk(key);
  }

  /** Const overload of `try_chunk(ChunkKey)`. */
  [[nodiscard]] auto try_chunk(ChunkKey key) const noexcept
      -> const page_type* {
    if (key.value >= chunk_count) {
      return nullptr;
    }
    return &chunk(key);
  }

  /** Returns a page by coordinate, or null when it is out of bounds. */
  [[nodiscard]] auto try_chunk(ChunkCoord3 coord) noexcept -> page_type* {
    if (!contains_chunk(coord)) {
      return nullptr;
    }
    return &chunk(coord);
  }

  /** Const overload of `try_chunk(ChunkCoord3)`. */
  [[nodiscard]] auto try_chunk(ChunkCoord3 coord) const noexcept
      -> const page_type* {
    if (!contains_chunk(coord)) {
      return nullptr;
    }
    return &chunk(coord);
  }

  /**
   * Returns mutable metadata by key.
   * @pre `key` is inside the world; debug builds assert this precondition.
   */
  [[nodiscard]] auto meta(ChunkKey key) noexcept -> ChunkMeta& {
    TESS_ASSERT(key.value < chunk_count);
    return metadata_[static_cast<std::size_t>(key.value)];
  }

  [[nodiscard]] auto meta(ChunkKey key) const noexcept -> const ChunkMeta& {
    TESS_ASSERT(key.value < chunk_count);
    return metadata_[static_cast<std::size_t>(key.value)];
  }

  [[nodiscard]] auto meta(ChunkCoord3 coord) noexcept -> ChunkMeta& {
    return meta(chunk_key<Shape>(coord));
  }

  [[nodiscard]] auto meta(ChunkCoord3 coord) const noexcept
      -> const ChunkMeta& {
    return meta(chunk_key<Shape>(coord));
  }

  /** Returns mutable metadata, or null when `key` is out of bounds. */
  [[nodiscard]] auto try_meta(ChunkKey key) noexcept -> ChunkMeta* {
    if (key.value >= chunk_count) {
      return nullptr;
    }
    return &meta(key);
  }

  [[nodiscard]] auto try_meta(ChunkKey key) const noexcept -> const ChunkMeta* {
    if (key.value >= chunk_count) {
      return nullptr;
    }
    return &meta(key);
  }

  [[nodiscard]] auto try_meta(ChunkCoord3 coord) noexcept -> ChunkMeta* {
    if (!contains_chunk(coord)) {
      return nullptr;
    }
    return &meta(coord);
  }

  [[nodiscard]] auto try_meta(ChunkCoord3 coord) const noexcept
      -> const ChunkMeta* {
    if (!contains_chunk(coord)) {
      return nullptr;
    }
    return &meta(coord);
  }

  [[nodiscard]] auto chunk_activity(ChunkKey key) const noexcept
      -> ChunkActivity {
    return active_mask(key).empty() ? ChunkActivity::Sleeping
                                    : ChunkActivity::Active;
  }

  [[nodiscard]] auto chunk_activity(ChunkCoord3 coord) const noexcept
      -> ChunkActivity {
    return chunk_activity(chunk_key<Shape>(coord));
  }

  [[nodiscard]] auto active_category_count(ChunkKey key) const noexcept
      -> std::uint32_t {
    return detail::popcount(active_mask(key));
  }

  // Hot-scan SoA columns split out of ChunkMeta; read-only -- mutate through
  // mark_/clear_/observe_.
  [[nodiscard]] auto dirty_mask(ChunkKey key) const noexcept -> DirtyMask {
    TESS_ASSERT(key.value < chunk_count);
    return dirty_masks_[static_cast<std::size_t>(key.value)];
  }

  [[nodiscard]] auto active_mask(ChunkKey key) const noexcept -> ActiveMask {
    TESS_ASSERT(key.value < chunk_count);
    return active_masks_[static_cast<std::size_t>(key.value)];
  }

  [[nodiscard]] auto dirty_bounds(ChunkKey key) const noexcept -> Box3 {
    TESS_ASSERT(key.value < chunk_count);
    return dirty_bounds_[static_cast<std::size_t>(key.value)];
  }

  void mark_dirty(ChunkKey key, DirtyMask mask, Box3 bounds) noexcept {
    TESS_ASSERT(key.value < chunk_count);
    const auto slot = static_cast<std::size_t>(key.value);
    detail::meta_mark_dirty(dirty_masks_[slot], dirty_bounds_[slot], meta(key),
                            mask, bounds);
  }

  /**
   * Advances the chunk content version without marking dirty work.
   *
   * Use this after a content write that must invalidate version-keyed
   * derived state but does not concern any dirty-mask consumer. This changes
   * only the content version. Use `mark_dirty` for dirty-metadata consumers,
   * `mark_topology_dirty` when topology freshness must change, and the
   * schedule's notification protocol when an OnDirty task must run. An
   * intervening call also makes an earlier `DirtyObservation` stale.
   *
   * The key must be valid, matching the precondition of `mark_dirty`.
   */
  void mark_content_changed(ChunkKey key) noexcept {
    TESS_ASSERT(key.value < chunk_count);
    detail::meta_mark_content_changed(meta(key));
  }

  void mark_topology_dirty(ChunkKey key, DirtyMask mask, Box3 bounds) noexcept {
    if (mask.empty()) {
      return;
    }
    mark_dirty(key, mask, bounds);
    ++meta(key).topology_version;
  }

  void mark_topology_rebuilt(ChunkKey key) noexcept {
    ++meta(key).topology_version;
  }

  void clear_dirty(ChunkKey key, DirtyMask mask) noexcept {
    TESS_ASSERT(key.value < chunk_count);
    const auto slot = static_cast<std::size_t>(key.value);
    detail::meta_clear_dirty(dirty_masks_[slot], dirty_bounds_[slot], mask);
  }

  [[nodiscard]] auto observe_dirty(ChunkKey key, DirtyMask mask) const noexcept
      -> DirtyObservation {
    TESS_ASSERT(key.value < chunk_count);
    const auto slot = static_cast<std::size_t>(key.value);
    return detail::meta_observe_dirty(dirty_masks_[slot], dirty_bounds_[slot],
                                      meta(key), mask);
  }

  // Clears exactly the observed mask iff the chunk's content version still
  // matches the observation. Any mark_dirty after the observation advances
  // that version, so a stale clear leaves every mask bit and bound in place
  // and returns false; the caller re-observes and rebuilds.
  bool clear_dirty_observed(ChunkKey key, DirtyObservation observed) noexcept {
    TESS_ASSERT(key.value < chunk_count);
    const auto slot = static_cast<std::size_t>(key.value);
    return detail::meta_clear_dirty_observed(
        dirty_masks_[slot], dirty_bounds_[slot], meta(key), observed);
  }

  void mark_active(ChunkKey key, ActiveMask mask) noexcept {
    TESS_ASSERT(key.value < chunk_count);
    const auto slot = static_cast<std::size_t>(key.value);
    detail::meta_mark_active(active_masks_[slot], mask);
  }

  void clear_active(ChunkKey key, ActiveMask mask) noexcept {
    TESS_ASSERT(key.value < chunk_count);
    const auto slot = static_cast<std::size_t>(key.value);
    detail::meta_clear_active(active_masks_[slot], mask);
  }

  /**
   * Appends matching dirty chunk keys to caller-owned storage.
   *
   * The scan is `O(chunk_count)`. It allocates only if `out` grows; reserve
   * enough capacity when allocation-free collection is required.
   */
  void collect_dirty_chunks(DirtyMask mask, std::vector<ChunkKey>& out) const {
    collect_matching_chunks(mask, dirty_masks_, out);
  }

  /**
   * Appends matching active chunk keys to caller-owned storage.
   *
   * The scan and allocation contract matches `collect_dirty_chunks()`.
   */
  void collect_active_chunks(ActiveMask mask,
                             std::vector<ChunkKey>& out) const {
    collect_matching_chunks(mask, active_masks_, out);
  }

  /** Returns matching dirty keys in a newly allocated vector. */
  [[nodiscard]] auto dirty_chunks(DirtyMask mask) const
      -> std::vector<ChunkKey> {
    std::vector<ChunkKey> chunks;
    collect_dirty_chunks(mask, chunks);
    return chunks;
  }

  /** Returns matching active keys in a newly allocated vector. */
  [[nodiscard]] auto active_chunks(ActiveMask mask) const
      -> std::vector<ChunkKey> {
    std::vector<ChunkKey> chunks;
    collect_active_chunks(mask, chunks);
    return chunks;
  }

  /**
   * Resolves a world coordinate without runtime error recovery.
   * @pre `coord` is inside the world; debug builds assert this precondition.
   */
  [[nodiscard]] auto resolve(Coord3 coord) const noexcept
      -> ResolvedTile<Shape> {
    TESS_ASSERT(contains<Shape>(coord));
    const auto chunk_coord_value = chunk_coord<Shape>(coord);
    return ResolvedTile<Shape>{
        chunk_key<Shape>(chunk_coord_value),
        local_tile_id<Shape>(local_coord<Shape>(coord)),
    };
  }

  /** Returns a resolved tile, or `std::nullopt` when out of bounds. */
  [[nodiscard]] auto try_resolve(Coord3 coord) const noexcept
      -> std::optional<ResolvedTile<Shape>> {
    if (!contains<Shape>(coord)) {
      return std::nullopt;
    }
    return resolve(coord);
  }

  /**
   * Returns mutable field storage without runtime error recovery.
   * @pre `coord` is inside the world and `Tag` belongs to the schema.
   */
  template <typename Tag>
  [[nodiscard]] auto field(Coord3 coord) noexcept
      -> Schema::template value_type<Tag>& {
    const auto resolved = resolve(coord);
    return chunk(resolved.chunk_key)
        .template field<Tag>(resolved.local_tile_id);
  }

  /** Const overload of `field()` with the same preconditions. */
  template <typename Tag>
  [[nodiscard]] auto field(Coord3 coord) const noexcept
      -> const Schema::template value_type<Tag>& {
    const auto resolved = resolve(coord);
    return chunk(resolved.chunk_key)
        .template field<Tag>(resolved.local_tile_id);
  }

  /**
   * Returns mutable field storage, or null when `coord` is out of bounds.
   * `Tag` must belong to the schema.
   */
  template <typename Tag>
  [[nodiscard]] auto try_field(Coord3 coord) noexcept
      -> Schema::template value_type<Tag>* {
    const auto resolved = try_resolve(coord);
    if (!resolved.has_value()) {
      return nullptr;
    }
    return &chunk(resolved->chunk_key)
                .template field<Tag>(resolved->local_tile_id);
  }

  /** Const overload of `try_field()` with the same checked behavior. */
  template <typename Tag>
  [[nodiscard]] auto try_field(Coord3 coord) const noexcept
      -> const Schema::template value_type<Tag>* {
    const auto resolved = try_resolve(coord);
    if (!resolved.has_value()) {
      return nullptr;
    }
    return &chunk(resolved->chunk_key)
                .template field<Tag>(resolved->local_tile_id);
  }

  /**
   * Returns the contiguous field column for a chunk without allocating.
   * @pre `key` is inside the world and `Tag` belongs to the schema.
   */
  template <typename Tag>
  [[nodiscard]] auto field_span(ChunkKey key) noexcept {
    return chunk(key).template field_span<Tag>();
  }

  /** Const overload of `field_span()` with the same lifetime contract. */
  template <typename Tag>
  [[nodiscard]] auto field_span(ChunkKey key) const noexcept {
    return chunk(key).template field_span<Tag>();
  }

 private:
  static constexpr bool contains_chunk(ChunkCoord3 coord) noexcept {
    using Traits = ShapeTraits<Shape>;
    return coord.x < Traits::chunk_count_x && coord.y < Traits::chunk_count_y &&
           coord.z < Traits::chunk_count_z;
  }

  // Scans a dense 4-byte mask column (16 chunks per cache line) instead of
  // streaming ChunkMeta structs.
  template <typename Mask>
  void collect_matching_chunks(Mask mask, const std::vector<Mask>& column,
                               std::vector<ChunkKey>& out) const {
    for (std::uint64_t key = 0; key < chunk_count; ++key) {
      if (static_cast<bool>(column[static_cast<std::size_t>(key)] & mask)) {
        out.push_back(ChunkKey{key});
      }
    }
  }

  std::vector<page_type> pages_;
  std::vector<ChunkMeta> metadata_;
  std::vector<DirtyMask> dirty_masks_;
  std::vector<ActiveMask> active_masks_;
  std::vector<Box3> dirty_bounds_;
};

/** Convenience alias for a dense world that owns every shaped chunk. */
template <typename Shape, typename Schema>
using AlwaysResidentWorld = World<Shape, Schema, AlwaysResident>;

}  // namespace tess
