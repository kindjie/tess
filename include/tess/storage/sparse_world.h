#pragma once

#include <tess/core/assert.h>
#include <tess/core/shape.h>
#include <tess/storage/chunk_meta.h>
#include <tess/storage/chunk_page.h>
#include <tess/storage/residency.h>
#include <tess/storage/world.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace tess {

namespace detail {

// Fixed-capacity map from ChunkKey to a resident slot index. When the residency
// capacity covers the complete key space, a direct slot array avoids hashing
// every tile access and uses less directory storage. Larger key spaces use an
// open-addressing table sized to twice the residency capacity (rounded up to a
// power of two). Neither representation reallocates after construction, and
// hashed deletion uses backward-shift compaction so no tombstones accumulate.
class ChunkDirectory {
 public:
  static constexpr std::size_t npos = static_cast<std::size_t>(-1);

  void reset(std::size_t capacity, std::uint64_t key_count) {
    if (key_count <= static_cast<std::uint64_t>(capacity)) {
      std::vector<Bucket>{}.swap(buckets_);
      direct_slots_.assign(static_cast<std::size_t>(key_count), npos);
      mask_ = 0;
      return;
    }

    std::vector<std::size_t>{}.swap(direct_slots_);
    std::size_t table = 2;
    while (table < capacity * 2) {
      table <<= 1u;
    }
    buckets_.assign(table, Bucket{});
    mask_ = table - 1;
  }

  [[nodiscard]] std::size_t find(ChunkKey key) const noexcept {
    // Hashed tables always have a nonzero mask (their minimum size is two), so
    // the mask is also the mode tag without adding another hot-path load.
    if (mask_ == 0) {
      if (key.value >= direct_slots_.size()) {
        return npos;
      }
      return direct_slots_[static_cast<std::size_t>(key.value)];
    }

    std::size_t i = home(key);
    while (buckets_[i].occupied) {
      if (buckets_[i].key == key) {
        return buckets_[i].slot;
      }
      i = (i + 1) & mask_;
    }
    return npos;
  }

  // Precondition: key is not already present and the table has a free bucket
  // (guaranteed while the number of resident chunks stays <= capacity).
  void insert(ChunkKey key, std::size_t slot) noexcept {
    if (mask_ == 0) {
      TESS_ASSERT(key.value < direct_slots_.size());
      direct_slots_[static_cast<std::size_t>(key.value)] = slot;
      return;
    }

    std::size_t i = home(key);
    while (buckets_[i].occupied) {
      i = (i + 1) & mask_;
    }
    buckets_[i] = Bucket{key, slot, true};
  }

  bool erase(ChunkKey key) noexcept {
    if (mask_ == 0) {
      if (key.value >= direct_slots_.size()) {
        return false;
      }
      auto& slot = direct_slots_[static_cast<std::size_t>(key.value)];
      if (slot == npos) {
        return false;
      }
      slot = npos;
      return true;
    }

    std::size_t i = home(key);
    while (buckets_[i].occupied && buckets_[i].key != key) {
      i = (i + 1) & mask_;
    }
    if (!buckets_[i].occupied) {
      return false;
    }
    std::size_t j = i;
    while (true) {
      j = (j + 1) & mask_;
      if (!buckets_[j].occupied) {
        break;
      }
      const std::size_t k = home(buckets_[j].key);
      // Move j back into the hole at i only when j's home slot is not
      // cyclically inside (i, j] — otherwise moving it would break its
      // probe chain.
      if (!in_cyclic_range(i, k, j)) {
        buckets_[i] = buckets_[j];
        i = j;
      }
    }
    buckets_[i] = Bucket{};
    return true;
  }

 private:
  struct Bucket {
    ChunkKey key{};
    std::size_t slot = 0;
    bool occupied = false;
  };

  [[nodiscard]] std::size_t home(ChunkKey key) const noexcept {
    return static_cast<std::size_t>(mix(key.value)) & mask_;
  }

  static std::uint64_t mix(std::uint64_t x) noexcept {
    x += 0x9e3779b97f4a7c15ull;
    x = (x ^ (x >> 30u)) * 0xbf58476d1ce4e5b9ull;
    x = (x ^ (x >> 27u)) * 0x94d049bb133111ebull;
    return x ^ (x >> 31u);
  }

  static bool in_cyclic_range(std::size_t lo, std::size_t pos,
                              std::size_t hi) noexcept {
    if (lo <= hi) {
      return lo < pos && pos <= hi;
    }
    return lo < pos || pos <= hi;
  }

  std::vector<Bucket> buckets_;
  std::vector<std::size_t> direct_slots_;
  std::size_t mask_ = 0;
};

}  // namespace detail

/**
 * Byte-budgeted owner of a sparse, least-recently-used chunk set.
 *
 * Only resident pages are materialized, so storage and iteration are bounded
 * by the configured capacity rather than `chunk_count`. Residency is explicit:
 * call `ensure_resident()` before unchecked access. Eviction resets the page
 * before reuse and logically invalidates all pointers, references, spans, slot
 * indices, and handles associated with that chunk. Use a `ResidencyHandle` when
 * validity must be checked across residency mutations.
 *
 * Construction allocates the fixed-capacity page and bookkeeping storage.
 * Residency changes and hot accessors allocate no Tess-owned storage
 * afterward; a field value's non-throwing default initializer may allocate.
 * Instances
 * are not internally synchronized: external synchronization is required when
 * residency or content can mutate; concurrent reads require an unchanged world.
 */
template <typename Shape, typename Schema>
class World<Shape, Schema, SparseResident> {
 public:
  using shape_type = Shape;
  using schema_type = Schema;
  using residency_type = SparseResident;
  using page_type = ChunkPage<Shape, Schema>;

  static constexpr std::uint64_t chunk_count = ShapeTraits<Shape>::chunk_count;
  static constexpr std::uint64_t local_tile_count =
      ShapeTraits<Shape>::local_tile_count;
  static constexpr std::size_t field_count = Schema::field_count;
  // Tess-owned inline page storage; field-managed dynamic or referenced
  // storage is outside this byte count.
  static constexpr std::size_t page_byte_size = page_type::byte_size;

  /**
   * Allocates fixed-capacity storage from `config` without materializing
   * chunks.
   *
   * Capacity is clamped to at least one page, even when the byte budget is
   * smaller than `page_byte_size`.
   */
  explicit World(ResidencyConfig config)
      : byte_budget_(config.byte_budget),
        capacity_(clamp_capacity(config.byte_budget)) {
    pages_.reserve(capacity_);
    for (std::size_t slot = 0; slot < capacity_; ++slot) {
      pages_.emplace_back(ChunkKey{0}, ChunkCoord3{});
    }
    metadata_.assign(capacity_, ChunkMeta{});
    dirty_masks_.assign(capacity_, DirtyMask{});
    active_masks_.assign(capacity_, ActiveMask{});
    dirty_bounds_.assign(capacity_, Box3{});
    slot_key_.assign(capacity_, ChunkKey{});
    slot_generation_.assign(capacity_, ResidencyGeneration{});
    lru_prev_.assign(capacity_, npos_slot);
    lru_next_.assign(capacity_, npos_slot);
    slot_position_.assign(capacity_, 0);

    resident_keys_.reserve(capacity_);
    resident_slots_.reserve(capacity_);
    free_slots_.reserve(capacity_);
    for (std::size_t slot = capacity_; slot-- > 0;) {
      free_slots_.push_back(slot);
    }
    directory_.reset(capacity_, chunk_count);
  }

  /** Returns the maximum number of simultaneously resident chunks. */
  [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }

  /** Returns the requested Tess-owned inline page-storage budget in bytes. */
  [[nodiscard]] std::size_t byte_budget() const noexcept {
    return byte_budget_;
  }
  /** Returns the current number of resident chunks. */
  [[nodiscard]] std::size_t resident_count() const noexcept {
    return resident_keys_.size();
  }
  /** Returns resident inline page bytes, excluding metadata and value-owned
   * dynamic or referenced storage. */
  [[nodiscard]] std::size_t resident_byte_size() const noexcept {
    return resident_keys_.size() * page_byte_size;
  }

  /** Sentinel returned by `resident_slot()` for a non-resident chunk. */
  static constexpr std::size_t npos_slot = detail::ChunkDirectory::npos;

  /** Returns whether `key` is inside the bounded shape. */
  [[nodiscard]] static constexpr bool contains(ChunkKey key) noexcept {
    return key.value < chunk_count;
  }

  /** Returns whether an in-bounds key currently has a resident page. */
  [[nodiscard]] bool is_resident(ChunkKey key) const noexcept {
    return directory_.find(key) != detail::ChunkDirectory::npos;
  }

  /**
   * Returns the fixed slot backing a resident chunk, or `npos_slot`.
   *
   * The slot identity remains valid only while the chunk stays resident. A
   * traversal may index by slot while holding the world logically const, but
   * must discard that indexing before any residency mutation.
   */
  [[nodiscard]] std::size_t resident_slot(ChunkKey key) const noexcept {
    return directory_.find(key);
  }

  /** Snapshot of residency facts returned by `resident_ref()`. */
  struct ResidentChunkRef {
    std::size_t slot = npos_slot;
    ResidencyGeneration generation{};
    const ChunkMeta* meta = nullptr;
  };

  /**
   * Returns slot, generation, and metadata with one directory lookup.
   *
   * For a missing chunk, `meta` is null, `generation` is zero, and `slot` is
   * `npos_slot`. The metadata pointer is valid only while the chunk remains
   * resident and no non-const world operation runs concurrently.
   */
  [[nodiscard]] auto resident_ref(ChunkKey key) const noexcept
      -> ResidentChunkRef {
    const auto slot = directory_.find(key);
    if (slot == detail::ChunkDirectory::npos) {
      return ResidentChunkRef{};
    }
    return ResidentChunkRef{slot, slot_generation_[slot], &metadata_[slot]};
  }

  /**
   * Returns a resident chunk's world-monotonic generation, or zero if absent.
   */
  [[nodiscard]] ResidencyGeneration residency_generation(
      ChunkKey key) const noexcept {
    const auto slot = directory_.find(key);
    if (slot == detail::ChunkDirectory::npos) {
      return ResidencyGeneration{};
    }
    return slot_generation_[slot];
  }

  /** Returns whether a handle still names its original residency interval. */
  [[nodiscard]] bool valid(ResidencyHandle handle) const noexcept {
    return handle.generation.valid() &&
           residency_generation(handle.key) == handle.generation;
  }

  /**
   * Borrows the current resident-key set without allocating.
   *
   * Order is unspecified. Any `ensure_resident()` or `evict()` call invalidates
   * the span's contents and iterators; copy keys when a snapshot is required.
   */
  [[nodiscard]] std::span<const ChunkKey> resident_chunk_keys() const noexcept {
    return {resident_keys_.data(), resident_keys_.size()};
  }

  /**
   * Returns an order-independent fingerprint of resident content and slots.
   *
   * It incorporates eviction/rematerialization generations, content versions,
   * keys, and
   * slot bindings, so consumers can probabilistically invalidate artifacts
   * indexed by resident slot. Computing it is `O(resident_count)`,
   * allocation-free, and never visits non-resident chunks. It is not a
   * persistent identifier or a collision-proof digest.
   */
  [[nodiscard]] std::uint64_t residency_fingerprint() const noexcept {
    const auto mix = [](std::uint64_t x) noexcept -> std::uint64_t {
      x = (x ^ (x >> 30u)) * 0xbf58476d1ce4e5b9ull;
      x = (x ^ (x >> 27u)) * 0x94d049bb133111ebull;
      return x ^ (x >> 31u);
    };
    // Slot-direct iteration: resident_slots_ pairs with resident_keys_, so
    // every term is a direct array read -- the by-key accessors would pay
    // three directory probes per chunk for the same data.
    auto acc = std::uint64_t{0};
    for (const auto slot : resident_slots_) {
      auto h = mix(slot_key_[slot].value);
      h ^= mix(h + static_cast<std::uint64_t>(slot));
      h ^= mix(h + slot_generation_[slot].value);
      h ^= mix(h + metadata_[slot].content_version.value);
      acc += h;
    }
    return mix(acc + static_cast<std::uint64_t>(resident_count()) +
               0x9e3779b97f4a7c15ull);
  }

  /**
   * Makes an in-bounds key resident and marks it most-recently used.
   *
   * When full, the least-recently-used chunk is evicted, invalidating its
   * handles and borrowed storage. A newly materialized page is
   * value-initialized and receives a new generation. Calling this for an
   * already resident key is
   * idempotent for data and generation. Tess performs no dynamic allocation
   * after construction; a field value's nothrow default initialization may
   * manage its own allocation while resetting a reused page.
   *
   * Returns an invalid handle for a key outside the bounded shape, matching
   * `try_chunk()`, `try_meta()` and `residency_generation()`. This is the
   * only residency entry point with no checked counterpart, so it is total
   * rather than asserted: the directory's direct-slot mode indexes its slot
   * table by the key, and an out-of-range key previously wrote past the end
   * wherever assertions were compiled out.
   */
  ResidencyHandle ensure_resident(ChunkKey key) {
    if (!contains(key)) {
      return ResidencyHandle{};
    }
    auto slot = directory_.find(key);
    if (slot != detail::ChunkDirectory::npos) {
      lru_move_to_mru(slot);
      return ResidencyHandle{key, slot_generation_[slot]};
    }
    slot = acquire_slot();
    pages_[slot].reset(key, chunk_coord<Shape>(key));
    metadata_[slot] = ChunkMeta{};
    dirty_masks_[slot] = DirtyMask{};
    active_masks_[slot] = ActiveMask{};
    dirty_bounds_[slot] = Box3{};
    slot_key_[slot] = key;
    slot_generation_[slot] = ++generation_clock_;
    lru_push_mru(slot);
    slot_position_[slot] = resident_keys_.size();
    resident_keys_.push_back(key);
    resident_slots_.push_back(slot);
    directory_.insert(key, slot);
    return ResidencyHandle{key, slot_generation_[slot]};
  }

  /** Marks a resident key most-recently used, or returns false if absent. */
  bool touch(ChunkKey key) noexcept {
    const auto slot = directory_.find(key);
    if (slot == detail::ChunkDirectory::npos) {
      return false;
    }
    lru_move_to_mru(slot);
    return true;
  }

  /**
   * Evicts a key immediately, or returns false if it is not resident.
   *
   * Success invalidates the chunk's handle, slot, page and metadata borrows,
   * field references, and resident-key views.
   */
  bool evict(ChunkKey key) {
    const auto slot = directory_.find(key);
    if (slot == detail::ChunkDirectory::npos) {
      return false;
    }
    release_slot(slot);
    free_slots_.push_back(slot);
    return true;
  }

  /**
   * Returns a resident page without runtime error recovery.
   * @pre `key` is resident; debug builds assert this precondition.
   */
  [[nodiscard]] auto chunk(ChunkKey key) noexcept -> page_type& {
    const auto slot = directory_.find(key);
    TESS_ASSERT(slot != detail::ChunkDirectory::npos);
    return pages_[slot];
  }

  /** Const overload of `chunk()` with the same residency precondition. */
  [[nodiscard]] auto chunk(ChunkKey key) const noexcept -> const page_type& {
    const auto slot = directory_.find(key);
    TESS_ASSERT(slot != detail::ChunkDirectory::npos);
    return pages_[slot];
  }

  /** Returns a resident page, or null for missing or out-of-bounds keys. */
  [[nodiscard]] auto try_chunk(ChunkKey key) noexcept -> page_type* {
    const auto slot = directory_.find(key);
    if (slot == detail::ChunkDirectory::npos) {
      return nullptr;
    }
    return &pages_[slot];
  }

  /** Const overload of `try_chunk()` with the same checked behavior. */
  [[nodiscard]] auto try_chunk(ChunkKey key) const noexcept
      -> const page_type* {
    const auto slot = directory_.find(key);
    if (slot == detail::ChunkDirectory::npos) {
      return nullptr;
    }
    return &pages_[slot];
  }

  /**
   * Returns resident metadata without runtime error recovery.
   * @pre `key` is resident; debug builds assert this precondition.
   */
  [[nodiscard]] auto meta(ChunkKey key) noexcept -> ChunkMeta& {
    const auto slot = directory_.find(key);
    TESS_ASSERT(slot != detail::ChunkDirectory::npos);
    return metadata_[slot];
  }

  [[nodiscard]] auto meta(ChunkKey key) const noexcept -> const ChunkMeta& {
    const auto slot = directory_.find(key);
    TESS_ASSERT(slot != detail::ChunkDirectory::npos);
    return metadata_[slot];
  }

  /** Returns resident metadata, or null when the key is not resident. */
  [[nodiscard]] auto try_meta(ChunkKey key) noexcept -> ChunkMeta* {
    const auto slot = directory_.find(key);
    if (slot == detail::ChunkDirectory::npos) {
      return nullptr;
    }
    return &metadata_[slot];
  }

  [[nodiscard]] auto try_meta(ChunkKey key) const noexcept -> const ChunkMeta* {
    const auto slot = directory_.find(key);
    if (slot == detail::ChunkDirectory::npos) {
      return nullptr;
    }
    return &metadata_[slot];
  }

  [[nodiscard]] auto chunk_activity(ChunkKey key) const noexcept
      -> ChunkActivity {
    return active_mask(key).empty() ? ChunkActivity::Sleeping
                                    : ChunkActivity::Active;
  }

  [[nodiscard]] auto active_category_count(ChunkKey key) const noexcept
      -> std::uint32_t {
    return detail::popcount(active_mask(key));
  }

  // Hot-scan SoA columns split out of ChunkMeta; read-only -- mutate through
  // mark_/clear_/observe_. The chunk
  // must be resident (same contract as meta()).
  [[nodiscard]] auto dirty_mask(ChunkKey key) const noexcept -> DirtyMask {
    return dirty_masks_[resident_slot_checked(key)];
  }

  [[nodiscard]] auto active_mask(ChunkKey key) const noexcept -> ActiveMask {
    return active_masks_[resident_slot_checked(key)];
  }

  [[nodiscard]] auto dirty_bounds(ChunkKey key) const noexcept -> Box3 {
    return dirty_bounds_[resident_slot_checked(key)];
  }

  void mark_dirty(ChunkKey key, DirtyMask mask, Box3 bounds) noexcept {
    const auto slot = resident_slot_checked(key);
    detail::meta_mark_dirty(dirty_masks_[slot], dirty_bounds_[slot],
                            metadata_[slot], mask, bounds);
  }

  /**
   * Advances the resident chunk's content version without marking dirty
   * work.
   *
   * Use this after a content write that must invalidate version-keyed
   * derived state but does not concern any dirty-mask consumer. This changes
   * only the content version. Use `mark_dirty` for dirty-metadata consumers,
   * `mark_topology_dirty` when topology freshness must change, and the
   * schedule's notification protocol when an OnDirty task must run. An
   * intervening call also makes an earlier `DirtyObservation` stale.
   *
   * The key must be resident, matching the precondition of `mark_dirty`.
   */
  void mark_content_changed(ChunkKey key) noexcept {
    const auto slot = resident_slot_checked(key);
    detail::meta_mark_content_changed(metadata_[slot]);
  }

  void mark_topology_dirty(ChunkKey key, DirtyMask mask, Box3 bounds) noexcept {
    if (mask.empty()) {
      return;
    }
    const auto slot = resident_slot_checked(key);
    detail::meta_mark_dirty(dirty_masks_[slot], dirty_bounds_[slot],
                            metadata_[slot], mask, bounds);
    ++metadata_[slot].topology_version;
  }

  void mark_topology_rebuilt(ChunkKey key) noexcept {
    ++meta(key).topology_version;
  }

  void clear_dirty(ChunkKey key, DirtyMask mask) noexcept {
    const auto slot = resident_slot_checked(key);
    detail::meta_clear_dirty(dirty_masks_[slot], dirty_bounds_[slot], mask);
  }

  [[nodiscard]] auto observe_dirty(ChunkKey key, DirtyMask mask) const noexcept
      -> DirtyObservation {
    const auto slot = resident_slot_checked(key);
    return detail::meta_observe_dirty(dirty_masks_[slot], dirty_bounds_[slot],
                                      metadata_[slot], mask,
                                      slot_generation_[slot]);
  }

  /**
   * Clears the observed mask when the observation is still current.
   *
   * Refuses an observation from an earlier residency interval: rematerializing
   * a chunk restarts its content version, so that value alone cannot
   * distinguish a mark this observation saw from one made after an eviction.
   */
  bool clear_dirty_observed(ChunkKey key, DirtyObservation observed) noexcept {
    const auto slot = resident_slot_checked(key);
    return detail::meta_clear_dirty_observed(
        dirty_masks_[slot], dirty_bounds_[slot], metadata_[slot], observed,
        slot_generation_[slot]);
  }

  void mark_active(ChunkKey key, ActiveMask mask) noexcept {
    const auto slot = resident_slot_checked(key);
    detail::meta_mark_active(active_masks_[slot], mask);
  }

  void clear_active(ChunkKey key, ActiveMask mask) noexcept {
    const auto slot = resident_slot_checked(key);
    detail::meta_clear_active(active_masks_[slot], mask);
  }

  /**
   * Appends matching resident dirty keys to caller-owned storage.
   *
   * The scan is `O(resident_count)` and allocates only if `out` grows.
   */
  void collect_dirty_chunks(DirtyMask mask, std::vector<ChunkKey>& out) const {
    collect_matching_chunks(mask, dirty_masks_, out);
  }

  /**
   * Appends matching resident active keys to caller-owned storage.
   *
   * The scan and allocation contract matches `collect_dirty_chunks()`.
   */
  void collect_active_chunks(ActiveMask mask,
                             std::vector<ChunkKey>& out) const {
    collect_matching_chunks(mask, active_masks_, out);
  }

  /** Returns matching resident dirty keys in a newly allocated vector. */
  [[nodiscard]] auto dirty_chunks(DirtyMask mask) const
      -> std::vector<ChunkKey> {
    std::vector<ChunkKey> chunks;
    collect_dirty_chunks(mask, chunks);
    return chunks;
  }

  /** Returns matching resident active keys in a newly allocated vector. */
  [[nodiscard]] auto active_chunks(ActiveMask mask) const
      -> std::vector<ChunkKey> {
    std::vector<ChunkKey> chunks;
    collect_active_chunks(mask, chunks);
    return chunks;
  }

  /**
   * Resolves an in-bounds coordinate without checking residency.
   * @pre `coord` is inside the world; debug builds assert this precondition.
   */
  [[nodiscard]] auto resolve(Coord3 coord) const noexcept
      -> ResolvedTile<Shape> {
    TESS_ASSERT(tess::contains<Shape>(coord));
    return ResolvedTile<Shape>{
        chunk_key<Shape>(chunk_coord<Shape>(coord)),
        local_tile_id<Shape>(local_coord<Shape>(coord)),
    };
  }

  /**
   * Returns a resolved tile, or `std::nullopt` when out of bounds.
   *
   * A successful result does not imply that the chunk is resident.
   */
  [[nodiscard]] auto try_resolve(Coord3 coord) const noexcept
      -> std::optional<ResolvedTile<Shape>> {
    if (!tess::contains<Shape>(coord)) {
      return std::nullopt;
    }
    return resolve(coord);
  }

  /**
   * Returns mutable field storage without runtime error recovery.
   * @pre `coord` is in bounds, its chunk is resident, and `Tag` is in schema.
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
   * Returns mutable field storage, or null if out of bounds or non-resident.
   * `Tag` must belong to the schema.
   */
  template <typename Tag>
  [[nodiscard]] auto try_field(Coord3 coord) noexcept
      -> Schema::template value_type<Tag>* {
    const auto resolved = try_resolve(coord);
    if (!resolved.has_value()) {
      return nullptr;
    }
    auto* page = try_chunk(resolved->chunk_key);
    if (page == nullptr) {
      return nullptr;
    }
    return &page->template field<Tag>(resolved->local_tile_id);
  }

  /** Const overload of `try_field()` with the same checked behavior. */
  template <typename Tag>
  [[nodiscard]] auto try_field(Coord3 coord) const noexcept
      -> const Schema::template value_type<Tag>* {
    const auto resolved = try_resolve(coord);
    if (!resolved.has_value()) {
      return nullptr;
    }
    const auto* page = try_chunk(resolved->chunk_key);
    if (page == nullptr) {
      return nullptr;
    }
    return &page->template field<Tag>(resolved->local_tile_id);
  }

  /**
   * Returns a resident chunk's contiguous field column without allocating.
   * @pre `key` is resident and `Tag` belongs to the schema.
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
  std::size_t acquire_slot() {
    if (!free_slots_.empty()) {
      const auto slot = free_slots_.back();
      free_slots_.pop_back();
      return slot;
    }
    return evict_least_recently_used();
  }

  // Pops the head of the intrusive LRU list in O(1). The victim is handed
  // straight back for reuse, so it is deliberately not returned to the free
  // list.
  std::size_t evict_least_recently_used() {
    TESS_ASSERT(!resident_slots_.empty());
    TESS_ASSERT(lru_head_ != npos_slot);
    const auto victim = lru_head_;
    release_slot(victim);
    return victim;
  }

  // Intrusive doubly-linked LRU over slot indices: lru_head_ is the
  // least-recently-used end (the eviction victim), lru_tail_ the
  // most-recently-used. All operations are O(1).
  void lru_unlink(std::size_t slot) noexcept {
    const auto prev = lru_prev_[slot];
    const auto next = lru_next_[slot];
    if (prev != npos_slot) {
      lru_next_[prev] = next;
    } else {
      lru_head_ = next;
    }
    if (next != npos_slot) {
      lru_prev_[next] = prev;
    } else {
      lru_tail_ = prev;
    }
    lru_prev_[slot] = npos_slot;
    lru_next_[slot] = npos_slot;
  }

  void lru_push_mru(std::size_t slot) noexcept {
    lru_prev_[slot] = lru_tail_;
    lru_next_[slot] = npos_slot;
    if (lru_tail_ != npos_slot) {
      lru_next_[lru_tail_] = slot;
    } else {
      lru_head_ = slot;
    }
    lru_tail_ = slot;
  }

  void lru_move_to_mru(std::size_t slot) noexcept {
    if (lru_tail_ == slot) {
      return;
    }
    lru_unlink(slot);
    lru_push_mru(slot);
  }

  // Removes a resident chunk from the directory and resident set. The caller
  // decides the slot's fate: explicit evict returns it to the free list;
  // eviction under budget pressure reuses it immediately.
  void release_slot(std::size_t slot) {
    lru_unlink(slot);
    directory_.erase(slot_key_[slot]);
    const auto position = slot_position_[slot];
    const auto last = resident_keys_.size() - 1;
    resident_keys_[position] = resident_keys_[last];
    resident_slots_[position] = resident_slots_[last];
    slot_position_[resident_slots_[position]] = position;
    resident_keys_.pop_back();
    resident_slots_.pop_back();
  }

  [[nodiscard]] static constexpr std::size_t clamp_capacity(
      std::size_t byte_budget) noexcept {
    if constexpr (page_byte_size == 0) {
      return 1;
    }
    const auto count = byte_budget / page_byte_size;
    return count < 1 ? 1 : count;
  }

  // Reads a dense 4-byte mask column by resident slot instead of streaming
  // ChunkMeta structs.
  template <typename Mask>
  void collect_matching_chunks(Mask mask, const std::vector<Mask>& column,
                               std::vector<ChunkKey>& out) const {
    for (const auto slot : resident_slots_) {
      if (static_cast<bool>(column[slot] & mask)) {
        out.push_back(slot_key_[slot]);
      }
    }
  }

  // Directory probe + the meta() residency contract, shared by the
  // mutation/accessor paths that index the SoA columns by slot.
  [[nodiscard]] std::size_t resident_slot_checked(ChunkKey key) const noexcept {
    const auto slot = directory_.find(key);
    TESS_ASSERT(slot != detail::ChunkDirectory::npos);
    return slot;
  }

  std::size_t byte_budget_;
  std::size_t capacity_;

  ResidencyGeneration generation_clock_{};

  std::vector<page_type> pages_;
  std::vector<ChunkMeta> metadata_;
  std::vector<DirtyMask> dirty_masks_;
  std::vector<ActiveMask> active_masks_;
  std::vector<Box3> dirty_bounds_;
  std::vector<ChunkKey> slot_key_;
  std::vector<ResidencyGeneration> slot_generation_;
  std::vector<std::size_t> lru_prev_;
  std::vector<std::size_t> lru_next_;
  std::size_t lru_head_ = npos_slot;
  std::size_t lru_tail_ = npos_slot;
  std::vector<std::size_t> slot_position_;

  std::vector<ChunkKey> resident_keys_;
  std::vector<std::size_t> resident_slots_;
  std::vector<std::size_t> free_slots_;
  detail::ChunkDirectory directory_;
};

/** Convenience alias for a byte-budgeted, explicitly resident world. */
template <typename Shape, typename Schema>
using SparseResidentWorld = World<Shape, Schema, SparseResident>;

}  // namespace tess
