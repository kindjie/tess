#pragma once

#include <tess/path/path.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace tess {

struct RouteCacheStats {
  std::size_t entries = 0;
  std::size_t hits = 0;
  std::size_t suffix_hits = 0;
  std::size_t misses = 0;
  std::size_t path_nodes = 0;
  std::size_t cap_invalidations = 0;
  std::size_t oversized_skips = 0;
};

// Exact (start, goal) lookups and same-goal suffix lookups are served by two
// open-addressed flat hash indexes (power-of-two capacity, linear probing)
// instead of linear scans. The suffix index is populated per stored
// Found-path node with first-write-wins, which preserves the earlier
// linear-scan determinism: the earliest stored entry containing a queried
// suffix node keeps winning. Both indexes are rebuilt from scratch on
// `invalidate()`/`clear()`. Storage is bounded by entry and path-node caps;
// an insert that would exceed either cap invalidates the whole cache first
// (matching the world-change invalidation lifecycle) and counts a cap
// invalidation in the stats, except a single route larger than the node cap,
// which is skipped outright (stats().oversized_skips) so it cannot evict
// resident entries and then violate the cap anyway. A cap of 0 disables
// storage; it does not mean "unlimited".
class RouteCacheScratch {
 public:
  static constexpr std::size_t default_max_entries = 512;
  static constexpr std::size_t default_max_path_nodes = std::size_t{1} << 20u;

  // A cap of 0 disables storage (every request recomputes); a single route
  // larger than max_path_nodes is skipped without disturbing resident
  // entries (counted in stats().oversized_skips).
  void set_caps(std::size_t max_entries, std::size_t max_path_nodes) noexcept {
    max_entries_ = max_entries;
    max_path_nodes_ = max_path_nodes;
  }

  void reserve_routes(std::size_t route_count) {
    entries_.reserve(route_count);
  }

  void reserve_path_nodes(std::size_t node_count) {
    paths_.reserve(node_count);
  }

  void clear() noexcept {
    invalidate();
    hits_ = 0;
    suffix_hits_ = 0;
    misses_ = 0;
    cap_invalidations_ = 0;
    oversized_skips_ = 0;
  }

  void invalidate() noexcept {
    entries_.clear();
    paths_.clear();
    exact_slots_.clear();
    suffix_slots_.clear();
    suffix_count_ = 0;
  }

  void reset_stats() noexcept {
    hits_ = 0;
    suffix_hits_ = 0;
    misses_ = 0;
    cap_invalidations_ = 0;
    oversized_skips_ = 0;
  }

  // The fingerprint identifies world CONTENT VERSIONS, not a world
  // instance: two same-shape worlds whose chunks carry identical version
  // counters (e.g. both populated without mark_dirty) alias, and a cache
  // reused across them would serve one world's routes for the other. Keep
  // one cache per world; only the sparse path self-identifies its world
  // (residency_generation is world-monotonic).
  template <typename World>
  void capture_world_versions(const World& world) noexcept {
    world_fingerprint_ = world_version_fingerprint(world);
    has_world_fingerprint_ = true;
  }

  template <typename World>
  [[nodiscard]] auto invalidate_if_world_changed(const World& world) noexcept
      -> bool {
    if (!has_world_fingerprint_) {
      capture_world_versions(world);
      return false;
    }
    const auto current = world_version_fingerprint(world);
    if (current == world_fingerprint_) {
      return false;
    }
    invalidate();
    world_fingerprint_ = current;
    has_world_fingerprint_ = true;
    return true;
  }

  [[nodiscard]] auto stats() const noexcept -> RouteCacheStats {
    return RouteCacheStats{
        entries_.size(),  hits_,         suffix_hits_,
        misses_,          paths_.size(), cap_invalidations_,
        oversized_skips_,
    };
  }

 private:
  struct Entry {
    Coord3 start{};
    Coord3 goal{};
    PathStatus status = PathStatus::NoPath;
    std::uint32_t cost = 0;
    std::size_t expanded_nodes = 0;
    std::size_t reached_nodes = 0;
    std::size_t path_offset = 0;
    std::size_t path_size = 0;
  };

  struct SuffixSlot {
    std::uint32_t entry_plus_one = 0;
    std::uint32_t offset = 0;
  };

  template <typename World, typename Tag>
  friend auto cached_astar_path(const World& world, PathRequest request,
                                PathScratch& scratch, RouteCacheScratch& cache)
      -> PathResult;

  // FNV-style lane combine with one final avalanche: cheap per stored path
  // node, well distributed for power-of-two linear probing.
  [[nodiscard]] static auto hash_pair(Coord3 first, Coord3 second) noexcept
      -> std::uint64_t {
    auto hash = std::uint64_t{0xcbf29ce484222325ull};
    hash = (hash ^ static_cast<std::uint64_t>(first.x)) * 0x100000001b3ull;
    hash = (hash ^ static_cast<std::uint64_t>(first.y)) * 0x100000001b3ull;
    hash = (hash ^ static_cast<std::uint64_t>(first.z)) * 0x100000001b3ull;
    hash = (hash ^ static_cast<std::uint64_t>(second.x)) * 0x100000001b3ull;
    hash = (hash ^ static_cast<std::uint64_t>(second.y)) * 0x100000001b3ull;
    hash = (hash ^ static_cast<std::uint64_t>(second.z)) * 0x100000001b3ull;
    hash = (hash ^ (hash >> 30u)) * 0xbf58476d1ce4e5b9ull;
    hash = (hash ^ (hash >> 27u)) * 0x94d049bb133111ebull;
    return hash ^ (hash >> 31u);
  }

  [[nodiscard]] auto find(PathRequest request) const noexcept -> const Entry* {
    if (exact_slots_.empty()) {
      return nullptr;
    }
    const auto mask = exact_slots_.size() - 1u;
    auto slot =
        static_cast<std::size_t>(hash_pair(request.start, request.goal)) & mask;
    while (exact_slots_[slot] != 0) {
      const auto& entry = entries_[exact_slots_[slot] - 1u];
      if (entry.start == request.start && entry.goal == request.goal) {
        return &entry;
      }
      slot = (slot + 1u) & mask;
    }
    return nullptr;
  }

  [[nodiscard]] auto find_suffix(PathRequest request,
                                 std::size_t& suffix_offset) const noexcept
      -> const Entry* {
    if (suffix_slots_.empty()) {
      return nullptr;
    }
    const auto mask = suffix_slots_.size() - 1u;
    auto slot =
        static_cast<std::size_t>(hash_pair(request.start, request.goal)) & mask;
    while (suffix_slots_[slot].entry_plus_one != 0) {
      const auto& candidate = suffix_slots_[slot];
      const auto& entry = entries_[candidate.entry_plus_one - 1u];
      if (entry.goal == request.goal &&
          paths_[entry.path_offset + candidate.offset] == request.start) {
        suffix_offset = candidate.offset;
        return &entry;
      }
      slot = (slot + 1u) & mask;
    }
    return nullptr;
  }

  void store(PathRequest request, const PathResult& result) {
    // Cap value 0 disables storage entirely, matching the portal segment
    // cache's budget semantics; it does not mean "unlimited".
    if (max_entries_ == 0 || max_path_nodes_ == 0) {
      return;
    }
    // A single result larger than the node cap can never fit; skip it
    // instead of invalidating resident entries and then violating the cap.
    if (result.path.size() > max_path_nodes_) {
      ++oversized_skips_;
      return;
    }
    if (entries_.size() + 1u > max_entries_ ||
        paths_.size() + result.path.size() > max_path_nodes_) {
      invalidate();
      ++cap_invalidations_;
    }
    const auto entry_index = entries_.size();
    const auto path_offset = paths_.size();
    paths_.insert(paths_.end(), result.path.begin(), result.path.end());
    entries_.push_back(Entry{
        request.start,
        request.goal,
        result.status,
        result.cost,
        result.expanded_nodes,
        result.reached_nodes,
        path_offset,
        result.path.size(),
    });
    exact_insert(entry_index);
    if (result.status == PathStatus::Found) {
      suffix_insert(entry_index);
    }
  }

  void exact_insert(std::size_t entry_index) {
    if (exact_slots_.size() < (entries_.size() + 1u) * 2u) {
      grow_exact_index();
      return;
    }
    exact_place(entry_index);
  }

  void exact_place(std::size_t entry_index) noexcept {
    const auto mask = exact_slots_.size() - 1u;
    const auto& entry = entries_[entry_index];
    auto slot =
        static_cast<std::size_t>(hash_pair(entry.start, entry.goal)) & mask;
    while (exact_slots_[slot] != 0) {
      slot = (slot + 1u) & mask;
    }
    exact_slots_[slot] = static_cast<std::uint32_t>(entry_index + 1u);
  }

  void grow_exact_index() {
    auto capacity = std::size_t{16};
    while (capacity < (entries_.size() + 1u) * 2u) {
      capacity *= 2u;
    }
    exact_slots_.assign(capacity, 0u);
    for (std::size_t i = 0; i < entries_.size(); ++i) {
      exact_place(i);
    }
  }

  // First-write-wins per (node, goal): the earliest stored entry containing
  // a node keeps serving suffix queries for it, matching the pre-index
  // linear-scan order.
  void suffix_insert(std::size_t entry_index) {
    const auto& entry = entries_[entry_index];
    if (suffix_slots_.size() < (suffix_count_ + entry.path_size + 1u) * 2u) {
      grow_suffix_index(entry.path_size);
    }
    for (std::size_t i = 0; i < entry.path_size; ++i) {
      suffix_place(entry_index, i);
    }
  }

  void suffix_place(std::size_t entry_index, std::size_t offset) noexcept {
    const auto mask = suffix_slots_.size() - 1u;
    const auto& entry = entries_[entry_index];
    const auto node = paths_[entry.path_offset + offset];
    auto slot = static_cast<std::size_t>(hash_pair(node, entry.goal)) & mask;
    while (suffix_slots_[slot].entry_plus_one != 0) {
      const auto& occupant = suffix_slots_[slot];
      const auto& occupant_entry = entries_[occupant.entry_plus_one - 1u];
      if (occupant_entry.goal == entry.goal &&
          paths_[occupant_entry.path_offset + occupant.offset] == node) {
        return;  // First write wins.
      }
      slot = (slot + 1u) & mask;
    }
    suffix_slots_[slot] = SuffixSlot{
        static_cast<std::uint32_t>(entry_index + 1u),
        static_cast<std::uint32_t>(offset),
    };
    ++suffix_count_;
  }

  void grow_suffix_index(std::size_t additional) {
    auto capacity = std::size_t{16};
    while (capacity < (suffix_count_ + additional + 1u) * 2u) {
      capacity *= 2u;
    }
    const auto old_slots = suffix_slots_;
    suffix_slots_.assign(capacity, SuffixSlot{});
    suffix_count_ = 0;
    for (const auto slot : old_slots) {
      if (slot.entry_plus_one != 0) {
        suffix_place(slot.entry_plus_one - 1u, slot.offset);
      }
    }
  }

  [[nodiscard]] auto path_span(const Entry& entry,
                               std::size_t offset = 0) const noexcept
      -> std::span<const Coord3> {
    if (entry.path_size <= offset) {
      return {};
    }
    return std::span<const Coord3>{paths_.data() + entry.path_offset + offset,
                                   entry.path_size - offset};
  }

  std::vector<Entry> entries_;
  std::vector<Coord3> paths_;
  std::vector<std::uint32_t> exact_slots_;
  std::vector<SuffixSlot> suffix_slots_;
  std::size_t suffix_count_ = 0;
  std::size_t max_entries_ = default_max_entries;
  std::size_t max_path_nodes_ = default_max_path_nodes;
  std::size_t hits_ = 0;
  std::size_t suffix_hits_ = 0;
  std::size_t misses_ = 0;
  std::size_t cap_invalidations_ = 0;
  std::size_t oversized_skips_ = 0;
  std::uint64_t world_fingerprint_ = 0;
  bool has_world_fingerprint_ = false;

  template <typename World>
  [[nodiscard]] static auto world_version_fingerprint(
      const World& world) noexcept -> std::uint64_t {
    if constexpr (std::is_same_v<typename World::residency_type,
                                 AlwaysResident>) {
      // Dense: fold every chunk's content version (meta().version) in order.
      auto fingerprint = std::uint64_t{0xcbf29ce484222325ull};
      for (std::uint64_t i = 0; i < World::chunk_count; ++i) {
        const auto version = world.meta(ChunkKey{i}).version;
        fingerprint ^= i + 0x9e3779b97f4a7c15ull + (fingerprint << 6u) +
                       (fingerprint >> 2u);
        fingerprint ^= version;
        fingerprint *= 0x100000001b3ull;
      }
      return fingerprint;
    } else {
      // Sparse: fold only the resident set (bounded by resident_count, never
      // chunk_count; meta()/residency_generation() are called only for keys
      // from resident_chunk_keys(), so never on a non-resident slot). Each
      // chunk contributes (key, residency_generation, content version):
      // version catches in-place edits, and residency_generation -- world-
      // monotonic and strictly greater on any reload, so it changes even when
      // ensure_resident resets version to 0 -- catches evict/reload/swap. The
      // per-key terms combine by a COMMUTATIVE sum, because
      // resident_chunk_keys() order is not stable (eviction swap-with-last
      // reorders it); an order- dependent chain would false-invalidate on a
      // mere reorder.
      const auto mix = [](std::uint64_t x) noexcept -> std::uint64_t {
        x = (x ^ (x >> 30u)) * 0xbf58476d1ce4e5b9ull;
        x = (x ^ (x >> 27u)) * 0x94d049bb133111ebull;
        return x ^ (x >> 31u);
      };
      auto acc = std::uint64_t{0};
      for (const auto key : world.resident_chunk_keys()) {
        auto h = mix(key.value);
        h ^= mix(h + world.residency_generation(key));
        h ^= mix(h + static_cast<std::uint64_t>(world.meta(key).version));
        acc += h;
      }
      return mix(acc + static_cast<std::uint64_t>(world.resident_count()) +
                 0x9e3779b97f4a7c15ull);
    }
  }
};

// Cache hits copy the cached route into `scratch.path_` and return a span
// into that scratch, never into cache-owned storage. Hit and miss results
// therefore share one lifetime contract: the span is valid until the next
// path call that uses the same `PathScratch`. Cache-internal storage may
// reallocate on any later miss without invalidating previously returned
// spans backed by other scratches.
//
// STALENESS IS THE CALLER'S JOB, on dense and sparse alike: this function
// never checks the world fingerprint (that costs O(chunk_count) per call by
// design), so after any world edit the caller must run
// cache.invalidate_if_world_changed(world) -- or invalidate()/clear() --
// before the next lookup, or a stale route can be served. PathRequestRuntime
// does this once per batch in prepare_process; direct callers own the same
// obligation.
template <typename World, typename Tag>
auto cached_astar_path(const World& world, PathRequest request,
                       PathScratch& scratch, RouteCacheScratch& cache)
    -> PathResult {
  // The cache stores absolute Coord3 keys and routes only (no residency-slot
  // state). Correctness on sparse rests entirely on the residency-aware
  // world_version_fingerprint plus prepare_process invalidating the whole cache
  // before any serve: any evict, reload, or in-place edit changes the
  // fingerprint and drops the cache, so a stale route can never be served. A
  // miss runs sparse-native astar_path.
  if (const auto* entry = cache.find(request); entry != nullptr) {
    ++cache.hits_;
    const auto cached = cache.path_span(*entry);
    scratch.path_.assign(cached.begin(), cached.end());
    return PathResult{
        entry->status,
        entry->cost,
        0,
        0,
        std::span<const Coord3>{scratch.path_},
    };
  }
  auto suffix_offset = std::size_t{0};
  if (const auto* entry = cache.find_suffix(request, suffix_offset);
      entry != nullptr) {
    ++cache.suffix_hits_;
    const auto suffix = cache.path_span(*entry, suffix_offset);
    scratch.path_.assign(suffix.begin(), suffix.end());
    return PathResult{
        PathStatus::Found,
        static_cast<std::uint32_t>(scratch.path_.size() - 1u),
        0,
        0,
        std::span<const Coord3>{scratch.path_},
    };
  }

  ++cache.misses_;
  const auto result = astar_path<World, Tag>(world, request, scratch);
  cache.store(request, result);
  return result;
}

}  // namespace tess
