#pragma once

#include <tess/core/tag_identity.h>
#include <tess/path/path.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <type_traits>
#include <vector>

namespace tess {

// How the unit route cache treats world edits between batches.
//
// WholeWorldExact: any chunk-version change anywhere drops every entry
// (today's behavior); served routes are always identical to fresh
// recomputation.
//
// ScopedFeasible: entries record the chunks their route crosses and are
// retired only when one of those chunks changes. Surviving routes are
// guaranteed LEGAL (every step walkable under the current world) with a
// truthful served cost, and they were optimal when stored — but an edit
// elsewhere that OPENS a shortcut can leave a served route suboptimal
// until it is naturally retired. Under blocking-only (graph-monotone)
// edits surviving routes remain optimal. Only unit-cost models without
// special transitions qualify for scoped footprints; other models' entries
// carry whole-world sensitivity and behave as in exact mode. Dense
// (AlwaysResident) worlds only; sparse worlds fall back to exact behavior.
/// Staleness policy for RouteCacheScratch (see enumerator comments).
enum class UnitRouteStaleness : std::uint8_t {
  WholeWorldExact,
  ScopedFeasible,
};

/// Snapshot of unit-route cache occupancy, hits, misses, and invalidations.
struct RouteCacheStats {
  // Resident entries INCLUDING scoped-mode tombstones awaiting compaction;
  // live_entries excludes them.
  std::size_t entries = 0;
  std::size_t hits = 0;
  std::size_t suffix_hits = 0;
  std::size_t misses = 0;
  std::size_t path_nodes = 0;
  std::size_t cap_invalidations = 0;
  std::size_t oversized_skips = 0;
  // Whole-cache drops forced by a lookup with a different movement class
  // than the cache was bound to (see cached_astar_path). Keep one cache per
  // (world, class) to stay at zero.
  std::size_t class_rebinds = 0;
  std::size_t provider_rebinds = 0;
  std::size_t live_entries = 0;
  // Scoped mode: dependency walks performed on first serve after an epoch
  // change, entries that survived one, and entries retired by one.
  std::size_t revalidations = 0;
  std::size_t scoped_survivals = 0;
  std::size_t retired_entries = 0;
};

// Exact (start, goal) lookups and same-goal suffix lookups are served by two
// open-addressed flat hash indexes (power-of-two capacity, linear probing)
// instead of linear scans. The suffix index is populated per stored
// Found-path node with first-LIVE-write-wins, which preserves the earlier
// linear-scan determinism: the earliest stored live entry containing a
// queried suffix node keeps winning (scoped-mode retirement frees a slot's
// claim; the next store covering that node re-owns it in place). Both
// indexes are rebuilt from scratch on `invalidate()`/`clear()`.
// Storage is bounded by entry and path-node caps;
// an insert that would exceed either cap invalidates the whole cache first
// (matching the world-change invalidation lifecycle) and counts a cap
// invalidation in the stats, except a single route larger than the node cap,
// which is skipped outright (stats().oversized_skips) so it cannot evict
// resident entries and then violate the cap anyway. A cap of 0 disables
// storage; it does not mean "unlimited".
// Stateful-provider bindings include object address plus revision so two live
// instances cannot alias. Copies/moves of the cache retain that external
// binding; the provider itself must remain at a stable address, and callers
// must clear bound caches before ending its lifetime.
/// Bounded scratch cache for exact and same-goal suffix unit routes.
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
    // The normal over-cap insertion policy invalidates the whole cache. Apply
    // that same deterministic policy immediately when a caller lowers either
    // cap below the live footprint; otherwise existing hits could bypass a
    // newly configured zero/smaller limit indefinitely.
    if (entries_.size() > max_entries_ || paths_.size() > max_path_nodes_) {
      invalidate();
      ++cap_invalidations_;
    }
  }

  void reserve_routes(std::size_t route_count) {
    entries_.reserve(route_count);
  }

  void reserve_path_nodes(std::size_t node_count) {
    paths_.reserve(node_count);
  }

  void clear() noexcept {
    invalidate();
    bound_class_ = 0;
    bound_provider_type_ = 0;
    bound_provider_instance_ = nullptr;
    bound_provider_revision_ = 0;
    hits_ = 0;
    suffix_hits_ = 0;
    misses_ = 0;
    cap_invalidations_ = 0;
    oversized_skips_ = 0;
    class_rebinds_ = 0;
    provider_rebinds_ = 0;
  }

  // Entries are keyed on (start, goal) — with staleness carried by the
  // world fingerprint (exact mode) or per-chunk dependency records (scoped
  // mode) — and nothing on the movement class, so the cache binds itself to
  // the class of each cached_astar_path call: a rebind drops every entry
  // (correct even on misuse) and counts in stats().class_rebinds. One cache
  // per (world, class) is the PERF contract, not a correctness precondition.
  void bind_class(std::uintptr_t identity) noexcept {
    if (bound_class_ == identity) {
      return;
    }
    if (bound_class_ != 0) {
      invalidate();
      ++class_rebinds_;
    }
    bound_class_ = identity;
  }

  void bind_provider(std::uintptr_t type_identity,
                     const void* instance_identity,
                     std::uint64_t revision) noexcept {
    if (bound_provider_type_ == type_identity &&
        bound_provider_instance_ == instance_identity &&
        bound_provider_revision_ == revision) {
      return;
    }
    if (bound_provider_type_ != 0) {
      invalidate();
      ++provider_rebinds_;
    }
    bound_provider_type_ = type_identity;
    bound_provider_instance_ = instance_identity;
    bound_provider_revision_ = revision;
  }

  void invalidate() noexcept {
    entries_.clear();
    paths_.clear();
    deps_.clear();
    exact_slots_.clear();
    suffix_slots_.clear();
    suffix_count_ = 0;
    dead_count_ = 0;
  }

  // Selects the staleness policy. Switching modes drops every entry
  // unconditionally — entries stored under one mode's semantics are never
  // served under the other's — independent of any runtime policy flag.
  void set_staleness(UnitRouteStaleness staleness) noexcept {
    if (staleness_ == staleness) {
      return;
    }
    invalidate();
    staleness_ = staleness;
  }

  [[nodiscard]] auto staleness() const noexcept -> UnitRouteStaleness {
    return staleness_;
  }

  // Per-route cap on stored dependency pairs (scoped mode). A single route
  // whose collapsed footprint alone exceeds the cap is skipped without
  // evicting residents, mirroring the oversized-path rule. Applies to
  // future stores; previously admitted footprints stay resident.
  void set_dependency_cap(std::size_t max_dependency_pairs) noexcept {
    max_dependency_pairs_ = max_dependency_pairs;
  }

  // Scoped-mode analog of invalidate_if_world_changed, and the single
  // staleness entry point for both modes: exact mode (and sparse worlds,
  // which scoped V1 excludes) delegates to the fingerprint drop; scoped
  // dense mode compares an exact per-chunk version snapshot — no hashing
  // in the staleness decision — and on any difference bumps the epoch that
  // lazy per-entry validation checks against. Returns true when a world
  // change was detected (entries dropped in exact mode; revalidation armed
  // in scoped mode).
  template <typename World>
  [[nodiscard]] auto refresh_if_world_changed(const World& world) -> bool {
    if constexpr (!std::is_same_v<typename World::residency_type,
                                  AlwaysResident>) {
      return invalidate_if_world_changed(world);
    } else {
      if (staleness_ != UnitRouteStaleness::ScopedFeasible) {
        return invalidate_if_world_changed(world);
      }
      if (version_snapshot_.size() != World::chunk_count) {
        version_snapshot_.resize(World::chunk_count);
        for (std::uint64_t i = 0; i < World::chunk_count; ++i) {
          version_snapshot_[i] = world.meta(ChunkKey{i}).version;
        }
        return false;
      }
      // Versions live inside per-chunk meta, not contiguously: compare and
      // update in one loop rather than gathering for a memcmp.
      auto changed = false;
      for (std::uint64_t i = 0; i < World::chunk_count; ++i) {
        const auto version = world.meta(ChunkKey{i}).version;
        if (version_snapshot_[i] != version) {
          version_snapshot_[i] = version;
          changed = true;
        }
      }
      if (!changed) {
        return false;
      }
      ++change_epoch_;
      if (change_epoch_ == 0) {
        invalidate();  // Epoch wrap: practically unreachable; clear anyway.
        ++change_epoch_;
      }
      return true;
    }
  }

  void reset_stats() noexcept {
    hits_ = 0;
    suffix_hits_ = 0;
    misses_ = 0;
    cap_invalidations_ = 0;
    oversized_skips_ = 0;
    revalidations_ = 0;
    scoped_survivals_ = 0;
    retired_entries_ = 0;
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
        entries_.size(),
        hits_,
        suffix_hits_,
        misses_,
        paths_.size(),
        cap_invalidations_,
        oversized_skips_,
        class_rebinds_,
        provider_rebinds_,
        entries_.size() - dead_count_,
        revalidations_,
        scoped_survivals_,
        retired_entries_,
    };
  }

 private:
  struct Entry {
    Coord3 start{};
    Coord3 goal{};
    PathStatus status = PathStatus::NoPath;
    std::uint32_t cost = 0;
    std::uint32_t cost_scale = 1;
    std::size_t expanded_nodes = 0;
    std::size_t reached_nodes = 0;
    std::size_t path_offset = 0;
    std::size_t path_size = 0;
    // Scoped mode only. whole_world marks entries with no sound chunk
    // footprint (non-Found results, ineligible transition models): they
    // fail validation on ANY epoch change — an empty dep list must never
    // read as "depends on nothing".
    std::size_t dep_offset = 0;
    std::size_t dep_count = 0;
    std::uint64_t validated_epoch = 0;
    bool alive = true;
    bool whole_world = false;
  };

  // One collapsed (chunk, captured content version) dependency of a stored
  // route; validation compares against the chunk's current version.
  struct DepPair {
    std::uint64_t key = 0;
    std::uint32_t version = 0;
  };

  struct SuffixSlot {
    std::uint32_t entry_plus_one = 0;
    std::uint32_t offset = 0;
  };

  template <typename World, typename Tag>
  friend auto cached_astar_path(const World& world, PathRequest request,
                                PathScratch& scratch, RouteCacheScratch& cache)
      -> PathResult;

  template <typename World, typename Tag, typename Provider>
  friend auto cached_astar_path(const World& world, PathRequest request,
                                PathScratch& scratch, RouteCacheScratch& cache,
                                const Provider& provider) -> PathResult;

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

  // Dead (retired) occupants do not terminate the probe: at most one LIVE
  // entry exists per exact key, so skipping tombstones cannot skip a match.
  [[nodiscard]] auto find(PathRequest request) noexcept -> Entry* {
    if (exact_slots_.empty()) {
      return nullptr;
    }
    const auto mask = exact_slots_.size() - 1u;
    auto slot =
        static_cast<std::size_t>(hash_pair(request.start, request.goal)) & mask;
    while (exact_slots_[slot] != 0) {
      auto& entry = entries_[exact_slots_[slot] - 1u];
      if (entry.alive && entry.start == request.start &&
          entry.goal == request.goal) {
        return &entry;
      }
      slot = (slot + 1u) & mask;
    }
    return nullptr;
  }

  [[nodiscard]] auto find_suffix(PathRequest request,
                                 std::size_t& suffix_offset) noexcept
      -> Entry* {
    if (suffix_slots_.empty()) {
      return nullptr;
    }
    const auto mask = suffix_slots_.size() - 1u;
    auto slot =
        static_cast<std::size_t>(hash_pair(request.start, request.goal)) & mask;
    while (suffix_slots_[slot].entry_plus_one != 0) {
      const auto& candidate = suffix_slots_[slot];
      auto& entry = entries_[candidate.entry_plus_one - 1u];
      if (entry.alive && entry.goal == request.goal &&
          paths_[entry.path_offset + candidate.offset] == request.start) {
        suffix_offset = candidate.offset;
        return &entry;
      }
      slot = (slot + 1u) & mask;
    }
    return nullptr;
  }

  void retire(Entry& entry) noexcept {
    entry.alive = false;
    ++dead_count_;
    ++retired_entries_;
    // Tombstones hold entries_/paths_ footprint until compaction; past
    // half-dead the whole cache is dropped (the same deterministic
    // lifecycle as a cap invalidation). NOTE: invalidate() empties
    // entries_, so the caller must not touch the entry afterward.
    if (dead_count_ * 2u > max_entries_ && max_entries_ != 0) {
      invalidate();
    }
  }

  // Scoped-mode serve gate: exact mode always serves; a scoped entry
  // already validated this epoch serves on one compare; otherwise its
  // dependency pairs are walked against current chunk versions —
  // whole-world entries fail unconditionally — and the entry is either
  // stamped or retired. Retiring here cannot orphan a better match: store
  // only runs after a live-match miss, so the retired entry was the only
  // live occupant for its key. On a false return the entry reference is
  // dead (and possibly dangling after compaction) — do not touch it.
  template <typename World>
  [[nodiscard]] auto validate_for_serve(const World& world,
                                        Entry& entry) noexcept -> bool {
    if (staleness_ != UnitRouteStaleness::ScopedFeasible) {
      return true;
    }
    if (entry.validated_epoch == change_epoch_) {
      return true;
    }
    if (entry.whole_world) {
      retire(entry);
      return false;
    }
    ++revalidations_;
    for (std::size_t i = 0; i < entry.dep_count; ++i) {
      const auto& dep = deps_[entry.dep_offset + i];
      if (world.meta(ChunkKey{dep.key}).version != dep.version) {
        retire(entry);
        return false;
      }
    }
    entry.validated_epoch = change_epoch_;
    ++scoped_survivals_;
    return true;
  }

  template <typename World, bool ScopeEligible>
  void store(const World& world, PathRequest request,
             const PathResult& result) {
    using Shape = typename World::shape_type;
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
    // Scoped mode: collapse the route's chunk footprint before touching
    // storage, so a footprint over the dependency cap is skipped without
    // evicting residents (the oversized-path rule, applied to deps).
    // Non-Found results and scope-ineligible models get no footprint —
    // they carry whole-world sensitivity instead (an empty dep list must
    // never mean "depends on nothing").
    const auto scoped =
        staleness_ == UnitRouteStaleness::ScopedFeasible &&
        std::is_same_v<typename World::residency_type, AlwaysResident>;
    const auto scoped_footprint =
        scoped && ScopeEligible && result.status == PathStatus::Found;
    dep_scratch_.clear();
    if (scoped_footprint) {
      auto previous = std::numeric_limits<std::uint64_t>::max();
      for (const auto node : result.path) {
        const auto key = chunk_key<Shape>(tile_key<Shape>(node)).value;
        if (key != previous) {
          dep_scratch_.push_back(key);
          previous = key;
        }
      }
      if (dep_scratch_.size() > max_dependency_pairs_) {
        ++oversized_skips_;
        return;
      }
    }
    if (entries_.size() + 1u > max_entries_ ||
        paths_.size() + result.path.size() > max_path_nodes_) {
      invalidate();
      ++cap_invalidations_;
    }
    const auto entry_index = entries_.size();
    const auto path_offset = paths_.size();
    const auto dep_offset = deps_.size();
    paths_.insert(paths_.end(), result.path.begin(), result.path.end());
    for (const auto key : dep_scratch_) {
      deps_.push_back(DepPair{key, world.meta(ChunkKey{key}).version});
    }
    entries_.push_back(Entry{
        request.start,
        request.goal,
        result.status,
        result.cost,
        result.cost_scale,
        result.expanded_nodes,
        result.reached_nodes,
        path_offset,
        result.path.size(),
        dep_offset,
        dep_scratch_.size(),
        change_epoch_,
        true,
        scoped && !scoped_footprint,
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

  // Index rebuilds drop tombstoned entries' slots (the natural compaction
  // point); the dead entries themselves stay in entries_ until a full
  // invalidation reclaims their footprint.
  void grow_exact_index() {
    auto capacity = std::size_t{16};
    while (capacity < (entries_.size() + 1u) * 2u) {
      capacity *= 2u;
    }
    exact_slots_.assign(capacity, 0u);
    for (std::size_t i = 0; i < entries_.size(); ++i) {
      if (entries_[i].alive) {
        exact_place(i);
      }
    }
  }

  // First-LIVE-write-wins per (node, goal): the earliest stored live entry
  // containing a node keeps serving suffix queries for it, matching the
  // pre-index linear-scan order. A dead occupant's claim is overwritten in
  // place so retirement can never suppress suffix reuse permanently.
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
        if (occupant_entry.alive) {
          return;  // First live write wins.
        }
        // Dead occupant for this (node, goal): reuse its slot in place,
        // preserving one-slot-per-(node, goal) with no chain growth.
        suffix_slots_[slot] = SuffixSlot{
            static_cast<std::uint32_t>(entry_index + 1u),
            static_cast<std::uint32_t>(offset),
        };
        return;
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
      if (slot.entry_plus_one != 0 &&
          entries_[slot.entry_plus_one - 1u].alive) {
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
  std::vector<DepPair> deps_;
  std::vector<std::uint64_t> dep_scratch_;
  std::vector<std::uint32_t> exact_slots_;
  std::vector<SuffixSlot> suffix_slots_;
  // Scoped mode: exact per-chunk content versions as of the last refresh
  // (never hashed), and the epoch lazy validation stamps against.
  std::vector<std::uint32_t> version_snapshot_;
  std::uint64_t change_epoch_ = 1;
  std::size_t dead_count_ = 0;
  std::size_t revalidations_ = 0;
  std::size_t scoped_survivals_ = 0;
  std::size_t retired_entries_ = 0;
  UnitRouteStaleness staleness_ = UnitRouteStaleness::WholeWorldExact;
  std::size_t suffix_count_ = 0;
  std::size_t max_entries_ = default_max_entries;
  std::size_t max_path_nodes_ = default_max_path_nodes;
  std::size_t max_dependency_pairs_ = default_max_path_nodes / 8u;
  std::size_t hits_ = 0;
  std::size_t suffix_hits_ = 0;
  std::size_t misses_ = 0;
  std::size_t cap_invalidations_ = 0;
  std::size_t oversized_skips_ = 0;
  std::size_t class_rebinds_ = 0;
  std::size_t provider_rebinds_ = 0;
  // Movement-class identity the entries are bound to (0 = unbound); see
  // bind_class.
  std::uintptr_t bound_class_ = 0;
  std::uintptr_t bound_provider_type_ = 0;
  const void* bound_provider_instance_ = nullptr;
  std::uint64_t bound_provider_revision_ = 0;
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
// STALENESS DETECTION IS THE CALLER'S JOB, on dense and sparse alike: this
// function never scans the world's versions itself (that costs
// O(chunk_count) per call by design), so after any world edit the caller
// must run cache.refresh_if_world_changed(world) — or the exact-mode
// invalidate_if_world_changed / invalidate()/clear() — before the next
// lookup, or a stale route can be served. PathRequestRuntime does this once
// per batch in prepare_process; direct callers own the same obligation. In
// ScopedFeasible mode the refresh arms per-entry dependency validation
// (performed inside this call at serve time) instead of dropping entries.
/// Runs unit A* with exact and same-goal suffix reuse from caller-owned cache.
template <typename World, typename Tag, typename Provider>
auto cached_astar_path(const World& world, PathRequest request,
                       PathScratch& scratch, RouteCacheScratch& cache,
                       const Provider& provider) -> PathResult {
  using Class = movement::movement_class_of<Tag>;
  using UnitClass = movement::detail::UnitMovementClass<Class>;
  using Model = ResolvedTransitionModel<World, UnitClass, Provider>;
  const auto model = Model{provider};
  // Bind the cache to this call's movement class (normalized, so a raw tag
  // and its WalkableField identity share entries): entries key on
  // (start, goal) only, so a direct caller alternating classes must never be
  // served the other class's route -- the rebind drops the cache instead.
  cache.bind_class(detail::tag_identity<movement::movement_class_of<Tag>>());
  cache.bind_provider(detail::tag_identity<Provider>(),
                      detail::transition_provider_instance_identity(provider),
                      model.revision());
  // The cache stores absolute Coord3 keys and routes only (no residency-slot
  // state). Correctness on sparse rests entirely on the residency-aware
  // world_version_fingerprint plus prepare_process invalidating the whole
  // cache before any serve — sparse worlds are excluded from ScopedFeasible
  // mode in V1 and always take the exact-fingerprint lifecycle: any evict,
  // reload, or in-place edit changes the fingerprint and drops the cache, so
  // a stale route can never be served. A miss runs sparse-native astar_path.
  //
  // Scope eligibility mirrors the suffix-reuse condition: with unit step
  // cost and no special transitions, every tile an accepted step reads lies
  // on the stored path, so the path's chunk footprint is the exact
  // feasibility dependency set. Other models' entries carry whole-world
  // sensitivity (retired on any epoch change), preserving exact-mode
  // behavior per entry.
  constexpr auto scope_eligible =
      Model::cost_scale == 1 && !Model::has_special_transitions;
  if (auto* entry = cache.find(request); entry != nullptr) {
    if (cache.validate_for_serve(world, *entry)) {
      ++cache.hits_;
      const auto cached = cache.path_span(*entry);
      scratch.path_.assign(cached.begin(), cached.end());
      return PathResult{
          entry->status,
          entry->cost,
          0,
          0,
          std::span<const Coord3>{scratch.path_},
          entry->cost_scale,
      };
    }
  }
  if constexpr (scope_eligible) {
    auto suffix_offset = std::size_t{0};
    if (auto* entry = cache.find_suffix(request, suffix_offset);
        entry != nullptr) {
      if (cache.validate_for_serve(world, *entry)) {
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
    }
  }

  ++cache.misses_;
  const auto result = [&] {
    if constexpr (std::is_same_v<Provider, AdjacentTransitions>) {
      return astar_path<World, Tag>(world, request, scratch,
                                    MissingChunkPolicy::TreatAsBlocked);
    } else {
      return astar_path<World, Tag, Provider>(
          world, request, scratch, MissingChunkPolicy::TreatAsBlocked,
          provider);
    }
  }();
  cache.template store<World, scope_eligible>(world, request, result);
  return result;
}

template <typename World, typename Tag>
/// Finds a cached empty-provider route or computes and stores one.
auto cached_astar_path(const World& world, PathRequest request,
                       PathScratch& scratch, RouteCacheScratch& cache)
    -> PathResult {
  return cached_astar_path<World, Tag, AdjacentTransitions>(
      world, request, scratch, cache, AdjacentTransitions{});
}

}  // namespace tess
