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
    slot_lru_.assign(capacity_, 0);
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

  // Monotonic counter bumped on every ensure_resident -- the only operation
  // that changes residency, and thus the slot->chunk bindings a resident-slot-
  // indexed artifact (e.g. a built distance field) depends on. A caller that
  // captures this before building and compares after can detect any eviction/
  // reload that happened in between. It also advances on a plain touch, which
  // merely over-invalidates (forces a rebuild) and never serves stale data.
  [[nodiscard]] std::uint64_t residency_epoch() const noexcept {
    return lru_clock_;
  }

  // Makes `key` resident (evicting the least-recently-used chunk if the
  // budget is full) and marks it most-recently-used. Idempotent: an already
  // resident chunk keeps its data and generation.
  ResidencyHandle ensure_resident(ChunkKey key) {
    TESS_ASSERT(contains(key));
    auto slot = directory_.find(key);
    if (slot != detail::ChunkDirectory::npos) {
      slot_lru_[slot] = ++lru_clock_;
      return ResidencyHandle{key, slot_generation_[slot]};
    }
    slot = acquire_slot();
    pages_[slot].reset(key, chunk_coord<Shape>(key));
    metadata_[slot] = ChunkMeta{};
    slot_key_[slot] = key;
    slot_generation_[slot] = ++generation_clock_;
    slot_lru_[slot] = ++lru_clock_;
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
    slot_lru_[slot] = ++lru_clock_;
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

  // Scans the resident slots (bounded by capacity) for the least-recently
  // ensured/touched chunk, releases it, and returns the freed slot ready to
  // be reassigned. O(resident_count) per eviction, which is bounded by the
  // byte budget; a streaming, miss-heavy workload over a very large budget
  // would benefit from an intrusive O(1) LRU list, deferred as an
  // optimization.
  std::size_t evict_least_recently_used() {
    TESS_ASSERT(!resident_slots_.empty());
    std::size_t victim = resident_slots_[0];
    std::uint64_t oldest = slot_lru_[victim];
    for (const auto slot : resident_slots_) {
      if (slot_lru_[slot] < oldest) {
        oldest = slot_lru_[slot];
        victim = slot;
      }
    }
    // The victim is handed straight back for reuse, so it is deliberately not
    // returned to the free list.
    release_slot(victim);
    return victim;
  }

  // Removes a resident chunk from the directory and resident set. The caller
  // decides the slot's fate: explicit evict returns it to the free list;
  // eviction under budget pressure reuses it immediately.
  void release_slot(std::size_t slot) {
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
  std::uint64_t lru_clock_ = 0;
  std::uint64_t generation_clock_ = 0;

  std::vector<page_type> pages_;
  std::vector<ChunkMeta> metadata_;
  std::vector<ChunkKey> slot_key_;
  std::vector<std::uint64_t> slot_generation_;
  std::vector<std::uint64_t> slot_lru_;
  std::vector<std::size_t> slot_position_;

  std::vector<ChunkKey> resident_keys_;
  std::vector<std::size_t> resident_slots_;
  std::vector<std::size_t> free_slots_;
  detail::ChunkDirectory directory_;
};

template <typename Shape, typename Schema>
using SparseResidentWorld = World<Shape, Schema, SparseResident>;

}  // namespace tess
