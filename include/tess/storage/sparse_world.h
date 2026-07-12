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

// Fixed-capacity open-addressing map from ChunkKey to a resident slot index.
// The table is sized to twice the residency capacity (rounded up to a power
// of two) and never rehashes, so lookups, inserts, and erases allocate
// nothing after construction. Deletion uses backward-shift compaction so no
// tombstones accumulate over long-lived evict/reload churn.
class ChunkDirectory {
 public:
  static constexpr std::size_t npos = static_cast<std::size_t>(-1);

  void reset(std::size_t capacity) {
    std::size_t table = 2;
    while (table < capacity * 2) {
      table <<= 1u;
    }
    buckets_.assign(table, Bucket{});
    mask_ = table - 1;
  }

  [[nodiscard]] std::size_t find(ChunkKey key) const noexcept {
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
    std::size_t i = home(key);
    while (buckets_[i].occupied) {
      i = (i + 1) & mask_;
    }
    buckets_[i] = Bucket{key, slot, true};
  }

  bool erase(ChunkKey key) noexcept {
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
  std::size_t mask_ = 0;
};

}  // namespace detail

// Byte-budgeted sparse world: only a resident subset of a (possibly enormous)
// bounded shape is materialized at any time, with least-recently-used
// eviction. Residency is managed explicitly through ensure_resident/touch;
// accessors assume the caller has made the chunk resident. All iteration is
// bounded by the resident set or the fixed slot capacity, never by
// chunk_count, so a world spanning trillions of chunks costs only its budget.
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
  static constexpr std::size_t page_byte_size = page_type::byte_size;

  explicit World(ResidencyConfig config)
      : byte_budget_(config.byte_budget),
        capacity_(clamp_capacity(config.byte_budget)) {
    pages_.reserve(capacity_);
    for (std::size_t slot = 0; slot < capacity_; ++slot) {
      pages_.emplace_back(ChunkKey{0}, ChunkCoord3{});
    }
    metadata_.assign(capacity_, ChunkMeta{});
    slot_key_.assign(capacity_, ChunkKey{});
    slot_generation_.assign(capacity_, 0);
    lru_prev_.assign(capacity_, npos_slot);
    lru_next_.assign(capacity_, npos_slot);
    slot_position_.assign(capacity_, 0);

    resident_keys_.reserve(capacity_);
    resident_slots_.reserve(capacity_);
    free_slots_.reserve(capacity_);
    for (std::size_t slot = capacity_; slot-- > 0;) {
      free_slots_.push_back(slot);
    }
    directory_.reset(capacity_);
  }

  [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
  [[nodiscard]] std::size_t byte_budget() const noexcept {
    return byte_budget_;
  }
  [[nodiscard]] std::size_t resident_count() const noexcept {
    return resident_keys_.size();
  }
  [[nodiscard]] std::size_t resident_byte_size() const noexcept {
    return resident_keys_.size() * page_byte_size;
  }

  // Sentinel returned by resident_slot for a non-resident chunk. Mirrors the
  // directory's npos so NodeIndexSpace can test residency without a second
  // lookup.
  static constexpr std::size_t npos_slot = detail::ChunkDirectory::npos;

  [[nodiscard]] static constexpr bool contains(ChunkKey key) noexcept {
    return key.value < chunk_count;
  }

  [[nodiscard]] bool is_resident(ChunkKey key) const noexcept {
    return directory_.find(key) != detail::ChunkDirectory::npos;
  }

  // Returns the fixed slot index backing a resident chunk, or npos_slot when
  // the chunk is not resident. The slot is stable for as long as the chunk
  // stays resident, so a search may index node arrays by slot for the whole of
  // a single traversal (the world is const during a search, so no eviction can
  // move the mapping mid-search).
  [[nodiscard]] std::size_t resident_slot(ChunkKey key) const noexcept {
    return directory_.find(key);
  }

  // One-probe bundle of the per-chunk residency facts: slot, generation,
  // and metadata. The individual accessors below each pay their own
  // directory probe, so a caller needing two or more facts about the same
  // key (the region-graph freshness checks do this per resident chunk per
  // pathing tick) should take this instead (audit 2026-07-11 M2). meta is
  // null (and generation 0, slot npos_slot) when the chunk is not resident.
  struct ResidentChunkRef {
    std::size_t slot = npos_slot;
    std::uint64_t generation = 0;
    const ChunkMeta* meta = nullptr;
  };

  [[nodiscard]] auto resident_ref(ChunkKey key) const noexcept
      -> ResidentChunkRef {
    const auto slot = directory_.find(key);
    if (slot == detail::ChunkDirectory::npos) {
      return ResidentChunkRef{};
    }
    return ResidentChunkRef{slot, slot_generation_[slot], &metadata_[slot]};
  }

  // Returns the resident chunk's generation, or 0 if it is not resident.
  // Generations are world-monotonic and never reused, so 0 is unambiguous.
  [[nodiscard]] std::uint64_t residency_generation(
      ChunkKey key) const noexcept {
    const auto slot = directory_.find(key);
    if (slot == detail::ChunkDirectory::npos) {
      return 0;
    }
    return slot_generation_[slot];
  }

  [[nodiscard]] bool valid(ResidencyHandle handle) const noexcept {
    return handle.generation != 0 &&
           residency_generation(handle.key) == handle.generation;
  }

  [[nodiscard]] std::span<const ChunkKey> resident_chunk_keys() const noexcept {
    return {resident_keys_.data(), resident_keys_.size()};
  }

  // Order-independent content fingerprint of the resident set: a commutative
  // sum over each resident chunk's (key, resident_slot, residency_generation,
  // content version -- meta().version, bumped on every in-place edit). Unlike a
  // bare counter it identifies the resident STATE itself, so it changes on any
  // eviction, reload, or in-place edit and --
  // because it reads actual content, not a per-world clock -- never collides
  // across two different worlds' residency (a counter starts low in every
  // world). A resident-slot-indexed artifact (e.g. a built distance field)
  // captures this before building and rejects a mismatch after, catching any
  // slot rebind (including one via world copy/swap or being read against the
  // wrong world) that would otherwise serve a stale path.
  //
  // resident_slot is folded in ADDITION to route_cache.h's
  // world_version_fingerprint terms: that cache is keyed by tile coordinate,
  // but a distance field is indexed by resident SLOT, so it also depends on the
  // key->slot binding. Two worlds can hold the same {key: generation, version}
  // set with the slots permuted (different eviction orders); including the slot
  // makes equal fingerprints imply identical slot indexing, so a match is truly
  // safe. Within one world the slot is redundant (a slot only changes via an
  // evict that drops the key or a reload that bumps the generation, both
  // already folded), so it never over-invalidates. O(resident_count), never
  // touches a non-resident chunk; commutative because resident_chunk_keys()
  // order is not stable (eviction swaps last in). The readers recompute this
  // per call; a batch that reads many requests against one unchanging resident
  // set could hoist it per batch -- a natural optimization if it shows in a
  // bench.
  [[nodiscard]] std::uint64_t residency_fingerprint() const noexcept {
    const auto mix = [](std::uint64_t x) noexcept -> std::uint64_t {
      x = (x ^ (x >> 30u)) * 0xbf58476d1ce4e5b9ull;
      x = (x ^ (x >> 27u)) * 0x94d049bb133111ebull;
      return x ^ (x >> 31u);
    };
    // Slot-direct iteration: resident_slots_ pairs with resident_keys_, so
    // every term is a direct array read -- the by-key accessors would pay
    // three directory probes per chunk for the same data (audit
    // 2026-07-11 M2).
    auto acc = std::uint64_t{0};
    for (const auto slot : resident_slots_) {
      auto h = mix(slot_key_[slot].value);
      h ^= mix(h + static_cast<std::uint64_t>(slot));
      h ^= mix(h + slot_generation_[slot]);
      h ^= mix(h + static_cast<std::uint64_t>(metadata_[slot].version));
      acc += h;
    }
    return mix(acc + static_cast<std::uint64_t>(resident_count()) +
               0x9e3779b97f4a7c15ull);
  }

  // Makes `key` resident (evicting the least-recently-used chunk if the
  // budget is full) and marks it most-recently-used. Idempotent: an already
  // resident chunk keeps its data and generation.
  ResidencyHandle ensure_resident(ChunkKey key) {
    TESS_ASSERT(contains(key));
    auto slot = directory_.find(key);
    if (slot != detail::ChunkDirectory::npos) {
      lru_move_to_mru(slot);
      return ResidencyHandle{key, slot_generation_[slot]};
    }
    slot = acquire_slot();
    pages_[slot].reset(key, chunk_coord<Shape>(key));
    metadata_[slot] = ChunkMeta{};
    slot_key_[slot] = key;
    slot_generation_[slot] = ++generation_clock_;
    lru_push_mru(slot);
    slot_position_[slot] = resident_keys_.size();
    resident_keys_.push_back(key);
    resident_slots_.push_back(slot);
    directory_.insert(key, slot);
    return ResidencyHandle{key, slot_generation_[slot]};
  }

  // Marks a resident chunk most-recently-used; returns false if not resident.
  bool touch(ChunkKey key) noexcept {
    const auto slot = directory_.find(key);
    if (slot == detail::ChunkDirectory::npos) {
      return false;
    }
    lru_move_to_mru(slot);
    return true;
  }

  // Releases a resident chunk immediately; returns false if not resident.
  bool evict(ChunkKey key) {
    const auto slot = directory_.find(key);
    if (slot == detail::ChunkDirectory::npos) {
      return false;
    }
    release_slot(slot);
    free_slots_.push_back(slot);
    return true;
  }

  [[nodiscard]] auto chunk(ChunkKey key) noexcept -> page_type& {
    const auto slot = directory_.find(key);
    TESS_ASSERT(slot != detail::ChunkDirectory::npos);
    return pages_[slot];
  }

  [[nodiscard]] auto chunk(ChunkKey key) const noexcept -> const page_type& {
    const auto slot = directory_.find(key);
    TESS_ASSERT(slot != detail::ChunkDirectory::npos);
    return pages_[slot];
  }

  [[nodiscard]] auto try_chunk(ChunkKey key) noexcept -> page_type* {
    const auto slot = directory_.find(key);
    if (slot == detail::ChunkDirectory::npos) {
      return nullptr;
    }
    return &pages_[slot];
  }

  [[nodiscard]] auto try_chunk(ChunkKey key) const noexcept
      -> const page_type* {
    const auto slot = directory_.find(key);
    if (slot == detail::ChunkDirectory::npos) {
      return nullptr;
    }
    return &pages_[slot];
  }

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

  [[nodiscard]] auto chunk_state(ChunkKey key) const noexcept -> ChunkState {
    return meta(key).state;
  }

  void set_chunk_state(ChunkKey key, ChunkState state) noexcept {
    meta(key).state = state;
  }

  void mark_dirty(ChunkKey key, std::uint32_t flags, Box3 bounds) noexcept {
    detail::meta_mark_dirty(meta(key), flags, bounds);
  }

  void mark_topology_dirty(ChunkKey key, std::uint32_t flags,
                           Box3 bounds) noexcept {
    if (flags == 0) {
      return;
    }
    auto& chunk_meta = meta(key);
    detail::meta_mark_dirty(chunk_meta, flags, bounds);
    ++chunk_meta.topology_version;
  }

  void mark_topology_rebuilt(ChunkKey key) noexcept {
    ++meta(key).topology_version;
  }

  void clear_dirty(ChunkKey key, std::uint32_t flags) noexcept {
    detail::meta_clear_dirty(meta(key), flags);
  }

  [[nodiscard]] auto observe_dirty(ChunkKey key,
                                   std::uint32_t flags) const noexcept
      -> DirtyObservation {
    return detail::meta_observe_dirty(meta(key), flags);
  }

  bool clear_dirty_observed(ChunkKey key, DirtyObservation observed) noexcept {
    return detail::meta_clear_dirty_observed(meta(key), observed);
  }

  void mark_active(ChunkKey key, std::uint32_t flags) noexcept {
    detail::meta_mark_active(meta(key), flags);
  }

  void clear_active(ChunkKey key, std::uint32_t flags) noexcept {
    detail::meta_clear_active(meta(key), flags);
  }

  void collect_dirty_chunks(std::uint32_t flags,
                            std::vector<ChunkKey>& out) const {
    collect_matching_chunks(flags, &ChunkMeta::field_dirty_flags, out);
  }

  void collect_active_chunks(std::uint32_t flags,
                             std::vector<ChunkKey>& out) const {
    collect_matching_chunks(flags, &ChunkMeta::active_flags, out);
  }

  [[nodiscard]] auto dirty_chunks(std::uint32_t flags) const
      -> std::vector<ChunkKey> {
    std::vector<ChunkKey> chunks;
    collect_dirty_chunks(flags, chunks);
    return chunks;
  }

  [[nodiscard]] auto active_chunks(std::uint32_t flags) const
      -> std::vector<ChunkKey> {
    std::vector<ChunkKey> chunks;
    collect_active_chunks(flags, chunks);
    return chunks;
  }

  [[nodiscard]] auto resolve(Coord3 coord) const noexcept
      -> ResolvedTile<Shape> {
    TESS_ASSERT(tess::contains<Shape>(coord));
    return ResolvedTile<Shape>{
        chunk_key<Shape>(chunk_coord<Shape>(coord)),
        local_tile_id<Shape>(local_coord<Shape>(coord)),
    };
  }

  [[nodiscard]] auto try_resolve(Coord3 coord) const noexcept
      -> std::optional<ResolvedTile<Shape>> {
    if (!tess::contains<Shape>(coord)) {
      return std::nullopt;
    }
    return resolve(coord);
  }

  template <typename Tag>
  [[nodiscard]] auto field(Coord3 coord) noexcept
      -> Schema::template value_type<Tag>& {
    const auto resolved = resolve(coord);
    return chunk(resolved.chunk_key)
        .template field<Tag>(resolved.local_tile_id);
  }

  template <typename Tag>
  [[nodiscard]] auto field(Coord3 coord) const noexcept
      -> const Schema::template value_type<Tag>& {
    const auto resolved = resolve(coord);
    return chunk(resolved.chunk_key)
        .template field<Tag>(resolved.local_tile_id);
  }

  // Residency-tolerant reader: returns nullptr when the coordinate is out of
  // bounds or its chunk is not resident.
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

  template <typename Tag>
  [[nodiscard]] auto field_span(ChunkKey key) noexcept {
    return chunk(key).template field_span<Tag>();
  }

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

  // Pops the head of the intrusive LRU list -- O(1) per eviction (audit
  // 2026-07-11 M11b; was an O(resident_count) timestamp scan). The victim
  // is handed straight back for reuse, so it is deliberately not returned
  // to the free list.
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
    if (page_byte_size == 0) {
      return 1;
    }
    const auto count = byte_budget / page_byte_size;
    return count < 1 ? 1 : count;
  }

  void collect_matching_chunks(std::uint32_t flags,
                               std::uint32_t ChunkMeta::* member,
                               std::vector<ChunkKey>& out) const {
    for (const auto slot : resident_slots_) {
      if ((metadata_[slot].*member & flags) != 0) {
        out.push_back(slot_key_[slot]);
      }
    }
  }

  std::size_t byte_budget_;
  std::size_t capacity_;

  std::uint64_t generation_clock_ = 0;

  std::vector<page_type> pages_;
  std::vector<ChunkMeta> metadata_;
  std::vector<ChunkKey> slot_key_;
  std::vector<std::uint64_t> slot_generation_;
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

template <typename Shape, typename Schema>
using SparseResidentWorld = World<Shape, Schema, SparseResident>;

}  // namespace tess
