#include <gtest/gtest.h>
#include <tess/tess.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "property_harness.h"

namespace {

namespace property = tess_test::property;

// The caches carry the richest stated invariants in the library and had
// no seeded coverage at all: every existing cache test drives a fixed
// hand-written sequence. An invariant that only breaks on an unusual
// interleaving of store, lookup, eviction and staleness had nothing
// looking for it.

struct PassableTag {};
// A second movement class, so a rebind is expressible at all.
struct OtherTag {};
struct CostTag {};
struct OtherCostTag {};

using CacheShape =
    tess::Shape<tess::Extent3{32, 32, 1}, tess::Extent3{16, 16, 1}>;
using CacheSchema = tess::FieldSchema<tess::Field<PassableTag, std::uint8_t>,
                                      tess::Field<OtherTag, std::uint8_t>,
                                      tess::Field<CostTag, std::uint32_t>,
                                      tess::Field<OtherCostTag, std::uint32_t>>;
using CacheWorld = tess::AlwaysResidentWorld<CacheShape, CacheSchema>;

/// Random store/lookup/evict/stale sequences over a budgeted product
/// cache.
class FieldProductCacheModel {
 public:
  // Six keys against a three-product budget, so the cache genuinely
  // fills and evicts. Equal-sized products keep the expected byte total
  // exact, which is what makes the budget bound checkable rather than
  // approximate.
  static constexpr std::uint32_t kKeys = 6;
  static constexpr std::size_t kBudgetProducts = 3;
  // Weighted, not uniform. Store and lookup must dominate: with a
  // world edit or a clear every few steps the cache is wiped before
  // recency can diverge, and an eviction test that never reaches a
  // discriminating state cannot tell LRU from FIFO.
  static constexpr std::uint32_t kActions = 9;
  static constexpr std::uint32_t kOperationCount = kKeys * kActions;

  FieldProductCacheModel() {
    // A uniformly passable world, so every build succeeds and the
    // sequence is about cache behaviour rather than pathfinding.
    for (auto& page : world_.chunks()) {
      for (auto& tile : page.template field_span<PassableTag>()) {
        tile = 1;
      }
    }
    product_bytes_ = build(0).byte_size();
    cache_.set_byte_budget(entry_budget());
  }

  void apply(std::uint32_t op) {
    const auto key = op % kKeys;
    switch (op / kKeys % kActions) {
      case 0:
      case 1:
      case 2:
        store(key);
        break;
      case 3:
      case 4:
      case 5:
        lookup(key);
        break;
      case 6:
        // Editing the world bumps a chunk version, so every product
        // built before it becomes stale. This is the only way the stale
        // path is reachable at all.
        touch_world();
        break;
      case 7:
        cache_.clear();
        resident_.clear();
        break;
      default:
        // A product that cannot possibly fit: the store must be
        // rejected outright and leave every existing entry alone.
        store_oversized(key);
        break;
    }
  }

  [[nodiscard]] auto check() const -> std::optional<property::Violation> {
    const auto stats = cache_.stats();
    if (stats.entries != 0 && stats.bytes > cache_byte_budget()) {
      return violation("a non-empty cache stays within its byte budget",
                       stats.bytes, cache_byte_budget());
    }
    if (stats.entries > kBudgetProducts) {
      return violation("entries never exceed what the budget affords",
                       stats.entries, kBudgetProducts);
    }
    // Every lookup increments exactly one of hits, misses and stale
    // rejections. A stale match is NOT a miss: it erases its entry and
    // reports a rejection, so the obvious hits + misses == lookups is
    // wrong and would fail here.
    if (stats.hits + stats.misses + stats.stale_rejections != lookups_) {
      return violation("every lookup is counted exactly once",
                       stats.hits + stats.misses + stats.stale_rejections,
                       lookups_);
    }
    if (stats.entries != resident_.size()) {
      return violation("the model tracks the cache's entry count",
                       stats.entries, resident_.size());
    }
    if (stats.bytes != resident_.size() * entry_bytes()) {
      return violation("byte accounting matches the resident products",
                       stats.bytes, resident_.size() * entry_bytes());
    }
    if (rejected_store_mutated_) {
      return violation("a store that cannot fit changes nothing", 1, 0);
    }
    if (stale_counted_as_miss_) {
      return violation("a stale match is a rejection, not a miss", 1, 0);
    }
    return std::nullopt;
  }

  /// The keys a probe should still find: resident AND still valid.
  ///
  /// Residency and validity are different things here. A world edit
  /// invalidates every cached product, but the entries stay resident
  /// until a lookup discovers the staleness and erases them — so a
  /// probe after an edit finds nothing even though the cache still
  /// holds entries. Conflating the two is a modelling error, and it is
  /// what this test caught on its first run.
  [[nodiscard]] auto expected_probe() const -> std::vector<std::uint32_t> {
    std::vector<std::uint32_t> valid;
    for (const auto key : resident_) {
      if (stored_version_[key] == world_version_) {
        valid.push_back(key);
      }
    }
    return valid;
  }

  [[nodiscard]] auto evictions() const -> std::size_t {
    return cache_.stats().evictions;
  }
  [[nodiscard]] auto stale_rejections() const -> std::size_t {
    return cache_.stats().stale_rejections;
  }
  [[nodiscard]] auto oversized_rejections() const -> std::size_t {
    return oversized_rejections_;
  }
  /// Rejections that happened while the cache still held entries --
  /// the only ones that actually test the preserve-residents rule.
  [[nodiscard]] auto rejections_with_residents() const -> std::size_t {
    return rejections_with_residents_;
  }
  [[nodiscard]] auto replacements() const -> std::size_t {
    return replacements_;
  }

  /// Direct drivers for tests that need a specific state rather than a
  /// random walk toward one.
  void store_key(std::uint32_t key) { store(key); }
  [[nodiscard]] auto entries() const -> std::size_t {
    return cache_.stats().entries;
  }
  [[nodiscard]] auto lookup_hits(std::uint32_t key) -> bool {
    const auto before = cache_.stats().hits;
    lookup(key);
    return cache_.stats().hits != before;
  }

  /// Probes every key once and returns those the cache still holds.
  ///
  /// Only safe at the END of a sequence: a lookup refreshes recency, so
  /// probing mid-sequence would corrupt the very order under test.
  /// Membership is unaffected by probing, so the terminal comparison is
  /// still sound.
  [[nodiscard]] auto probe_resident() -> std::vector<std::uint32_t> {
    std::vector<std::uint32_t> found;
    for (std::uint32_t key = 0; key < kKeys; ++key) {
      if (cache_.lookup<CacheWorld, PassableTag>(world_, goals_for(key)) !=
          nullptr) {
        found.push_back(key);
      }
    }
    return found;
  }

 private:
  static auto violation(const char* name, std::uint64_t observed,
                        std::uint64_t expected)
      -> std::optional<property::Violation> {
    std::ostringstream detail;
    detail << "observed " << observed << ", expected " << expected;
    return property::Violation{name, detail.str(), 0};
  }

  [[nodiscard]] auto goals_for(std::uint32_t key) const -> tess::GoalSet {
    tess::GoalSet goals;
    goals.add(tess::Coord3{static_cast<std::int32_t>(key * 3 + 1),
                           static_cast<std::int32_t>(key * 2 + 1), 0});
    return goals;
  }

  [[nodiscard]] auto build(std::uint32_t key) -> tess::DistanceFieldProduct {
    tess::DistanceFieldProduct product;
    product.reserve_goals(1);
    product.reserve_dependencies(CacheWorld::chunk_count);
    (void)tess::build_distance_field_product<CacheWorld, PassableTag>(
        world_, goals_for(key), scratch_, product);
    return product;
  }

  // The cache adds per-entry overhead on top of the product's own bytes,
  // so the budget is set from a measured entry rather than a guess.
  [[nodiscard]] auto entry_bytes() const -> std::size_t {
    return measured_entry_bytes_;
  }
  [[nodiscard]] auto entry_budget() const -> std::size_t {
    return kBudgetProducts * (product_bytes_ + kEntryOverheadAllowance);
  }
  [[nodiscard]] auto cache_byte_budget() const -> std::size_t {
    return entry_budget();
  }

  void store(std::uint32_t key) {
    auto product = build(key);
    if (product.status() != tess::PathStatus::Found) {
      return;
    }
    const auto was_resident =
        std::find(resident_.begin(), resident_.end(), key) != resident_.end();
    const auto stored =
        cache_.store<CacheWorld, PassableTag>(std::move(product));
    if (!stored) {
      return;
    }
    if (measured_entry_bytes_ == 0) {
      measured_entry_bytes_ = cache_.stats().bytes;
    }
    if (was_resident) {
      ++replacements_;
    }
    // Freshly built against the current world, so this entry is valid
    // until the next edit.
    stored_version_[key] = world_version_;
    touch_resident(key);
    // The cache evicts the least-recently-used entry to stay inside the
    // budget; the model applies the same rule to its own order, so a
    // divergence in WHICH entry left shows up as a count mismatch here
    // and as a set difference in the terminal probe.
    while (resident_.size() > kBudgetProducts) {
      resident_.erase(resident_.begin());
    }
  }

  // A candidate too large for the whole budget, offered to a cache that
  // still holds its residents.
  //
  // The first version of this lowered the budget to one byte, which
  // evicted everything before the store was attempted -- so it only
  // ever proved that an oversized product is refused by an EMPTY cache,
  // and the invariant it claimed to test (a rejected store preserves
  // existing entries) was checked against nothing. The candidate is now
  // made oversized by carrying thousands of goals, which inflates its
  // entry beyond the budget while every resident stays exactly where it
  // was.
  void store_oversized(std::uint32_t key) {
    const auto before = cache_.stats();
    auto product = build_oversized(key);
    if (product.status() != tess::PathStatus::Found) {
      return;
    }
    const auto stored =
        cache_.store<CacheWorld, PassableTag>(std::move(product));
    const auto after = cache_.stats();
    if (stored) {
      rejected_store_mutated_ = true;
      return;
    }
    ++oversized_rejections_;
    if (after.entries != before.entries || after.bytes != before.bytes ||
        after.evictions != before.evictions) {
      rejected_store_mutated_ = true;
    }
    if (before.entries != 0) {
      ++rejections_with_residents_;
    }
  }

  [[nodiscard]] auto build_oversized(std::uint32_t key)
      -> tess::DistanceFieldProduct {
    tess::GoalSet goals;
    goals.reserve(kOversizedGoals);
    for (std::size_t i = 0; i < kOversizedGoals; ++i) {
      const auto x = static_cast<std::int32_t>((key + i) % 31 + 1);
      const auto y = static_cast<std::int32_t>((key + i / 31) % 31 + 1);
      goals.add(tess::Coord3{x, y, 0});
    }
    tess::DistanceFieldProduct product;
    product.reserve_goals(kOversizedGoals);
    product.reserve_dependencies(CacheWorld::chunk_count);
    (void)tess::build_distance_field_product<CacheWorld, PassableTag>(
        world_, goals, scratch_, product);
    return product;
  }

  void lookup(std::uint32_t key) {
    const auto before = cache_.stats();
    ++lookups_;
    const auto* found =
        cache_.lookup<CacheWorld, PassableTag>(world_, goals_for(key));
    const auto after = cache_.stats();
    if (found != nullptr) {
      touch_resident(key);
      return;
    }
    if (after.stale_rejections != before.stale_rejections) {
      // A stale match erases its entry and must not count as a miss.
      if (after.misses != before.misses) {
        stale_counted_as_miss_ = true;
      }
      drop_resident(key);
    }
  }

  // Marking a chunk dirty bumps its version, which is what a product's
  // dependency snapshot validates against. Every cached product depends
  // on this chunk, so all of them become stale at once -- but they stay
  // resident until a lookup discovers it, which is exactly the
  // interleaving worth randomizing.
  void touch_world() {
    world_.mark_dirty(
        tess::ChunkKey{0}, 1U,
        tess::Box3{tess::Coord3{0, 0, 0}, tess::Extent3{1, 1, 1}});
    ++world_version_;
  }

  void touch_resident(std::uint32_t key) {
    drop_resident(key);
    resident_.push_back(key);
  }

  void drop_resident(std::uint32_t key) {
    resident_.erase(std::remove(resident_.begin(), resident_.end(), key),
                    resident_.end());
  }

  static constexpr std::size_t kEntryOverheadAllowance = 512;
  // Enough goals that the entry's own goal storage dwarfs the whole
  // budget, so the candidate cannot fit however empty the cache is.
  static constexpr std::size_t kOversizedGoals = 4096;

  CacheWorld world_{};
  tess::DistanceFieldScratch scratch_{};
  tess::FieldProductCache cache_{};
  std::vector<std::uint32_t> resident_;
  std::size_t product_bytes_ = 0;
  std::size_t measured_entry_bytes_ = 0;
  std::size_t lookups_ = 0;
  std::size_t oversized_rejections_ = 0;
  std::size_t rejections_with_residents_ = 0;
  std::size_t replacements_ = 0;
  std::uint64_t world_version_ = 0;
  std::vector<std::uint64_t> stored_version_ =
      std::vector<std::uint64_t>(kKeys, 0);
  bool rejected_store_mutated_ = false;
  bool stale_counted_as_miss_ = false;
};

// Route caching is bounded by two independent caps and, unlike the
// product cache, has NO eviction: an insert that would breach either cap
// invalidates the whole cache. That policy plus first-write-wins suffix
// reuse and the bind-and-drop rules had only fixed hand-written
// sequences before this.
class RouteCacheModel {
 public:
  static constexpr std::uint32_t kRoutes = 5;
  static constexpr std::uint32_t kActions = 6;
  static constexpr std::uint32_t kOperationCount = kRoutes * kActions;
  static constexpr std::size_t kMaxEntries = 3;
  // Sized against the routes actually generated: a normal route is
  // roughly fifteen nodes, so three of them breach this and exercise
  // the wholesale cap invalidation, while the corner-to-corner route
  // (63 nodes on a 32x32 grid) exceeds it on its own and is skipped.
  // The first draft used 64 here, which the corner route fit inside --
  // so nothing was ever skipped and the gate caught it.
  static constexpr std::size_t kMaxPathNodes = 40;

  RouteCacheModel() {
    for (auto& page : world_.chunks()) {
      for (auto& tile : page.template field_span<PassableTag>()) {
        tile = 1;
      }
    }
    scratch_.reserve_nodes(256);
    cache_.reserve_routes(kMaxEntries);
    cache_.reserve_path_nodes(kMaxPathNodes);
    cache_.set_caps(kMaxEntries, kMaxPathNodes);
  }

  void apply(std::uint32_t op) {
    const auto route = op % kRoutes;
    switch (op / kRoutes % kActions) {
      case 0:
      case 1:
      case 2:
        query(route);
        break;
      case 3:
        // A route longer than the node cap must be skipped outright,
        // leaving resident entries alone -- the cache's only
        // "reject without disturbing anything" path.
        query_oversized();
        break;
      case 4:
        // Rebinding to another movement class drops every entry,
        // because entries key on (start, goal) and carry no class.
        rebind_class(route % 2 == 0);
        break;
      default:
        cache_.invalidate();
        break;
    }
  }

  [[nodiscard]] auto check() const -> std::optional<property::Violation> {
    const auto stats = cache_.stats();
    // Both caps are hard bounds. There is no eviction to soften them:
    // an insert that would breach either wipes the cache instead.
    if (stats.entries > kMaxEntries) {
      return violation("entries stay within the entry cap", stats.entries,
                       kMaxEntries);
    }
    if (stats.path_nodes > kMaxPathNodes) {
      return violation("stored path nodes stay within the node cap",
                       stats.path_nodes, kMaxPathNodes);
    }
    // Every query resolves as exactly one of exact hit, suffix hit, or
    // miss.
    if (stats.hits + stats.suffix_hits + stats.misses != queries_) {
      return violation("every query is counted exactly once",
                       stats.hits + stats.suffix_hits + stats.misses, queries_);
    }
    if (stats.entries == 0 && stats.path_nodes != 0) {
      return violation("an empty cache holds no path storage", stats.path_nodes,
                       0);
    }
    if (oversized_disturbed_) {
      return violation(
          "an oversized route is skipped without disturbing "
          "resident entries",
          1, 0);
    }
    if (hit_reported_search_) {
      return violation("a cache hit reports no search work", 1, 0);
    }
    return std::nullopt;
  }

  [[nodiscard]] auto oversized_skips() const -> std::size_t {
    return cache_.stats().oversized_skips;
  }
  [[nodiscard]] auto skips_with_residents() const -> std::size_t {
    return skips_with_residents_;
  }
  [[nodiscard]] auto cap_invalidations() const -> std::size_t {
    return cache_.stats().cap_invalidations;
  }
  [[nodiscard]] auto class_rebinds() const -> std::size_t {
    return cache_.stats().class_rebinds;
  }
  [[nodiscard]] auto suffix_hits() const -> std::size_t {
    return cache_.stats().suffix_hits;
  }
  [[nodiscard]] auto hits() const -> std::size_t { return cache_.stats().hits; }

 private:
  static auto violation(const char* name, std::uint64_t observed,
                        std::uint64_t expected)
      -> std::optional<property::Violation> {
    std::ostringstream detail;
    detail << "observed " << observed << ", expected " << expected;
    return property::Violation{name, detail.str(), 0};
  }

  // Routes sharing a goal column, so a later query starting on an
  // earlier route's interior node can be served from its suffix.
  [[nodiscard]] static auto request_for(std::uint32_t route)
      -> tess::PathRequest {
    const auto x = static_cast<std::int64_t>(route);
    return tess::PathRequest{tess::Coord3{x, 0, 0}, tess::Coord3{7, 7, 0}};
  }

  void query(std::uint32_t route) {
    const auto before = cache_.stats();
    ++queries_;
    const auto result = run(request_for(route));
    const auto after = cache_.stats();
    const auto was_hit =
        after.hits != before.hits || after.suffix_hits != before.suffix_hits;
    // A served route reports no expanded or reached nodes: that zeroed
    // pair is the observable signature of not having searched.
    if (was_hit && (result.expanded_nodes != 0 || result.reached_nodes != 0)) {
      hit_reported_search_ = true;
    }
  }

  void query_oversized() {
    const auto before = cache_.stats();
    ++queries_;
    // A path far longer than the node cap: it must be skipped rather
    // than stored, and must not disturb what is already resident.
    (void)run(
        tess::PathRequest{tess::Coord3{0, 0, 0}, tess::Coord3{31, 31, 0}});
    const auto after = cache_.stats();
    if (after.oversized_skips == before.oversized_skips) {
      return;
    }
    // cached_astar_path binds the movement class at the START of the
    // call, and a rebind drops every entry by design. So a call that
    // both rebound and skipped legitimately empties the cache, and
    // attributing that loss to the skip would be a modelling error --
    // which is exactly what the harness caught here, shrinking to the
    // four-operation sequence 20,16,21,18.
    if (after.class_rebinds != before.class_rebinds ||
        after.provider_rebinds != before.provider_rebinds) {
      return;
    }
    if (before.entries != 0) {
      ++skips_with_residents_;
      if (after.entries != before.entries ||
          after.path_nodes != before.path_nodes) {
        oversized_disturbed_ = true;
      }
    }
  }

  auto run(tess::PathRequest request) -> tess::PathResult {
    if (alternate_class_) {
      return tess::cached_astar_path<CacheWorld, OtherTag>(world_, request,
                                                           scratch_, cache_);
    }
    return tess::cached_astar_path<CacheWorld, PassableTag>(world_, request,
                                                            scratch_, cache_);
  }

  void rebind_class(bool alternate) { alternate_class_ = alternate; }

  CacheWorld world_{};
  tess::PathScratch scratch_{};
  tess::RouteCacheScratch cache_{};
  std::size_t queries_ = 0;
  std::size_t skips_with_residents_ = 0;
  bool oversized_disturbed_ = false;
  bool hit_reported_search_ = false;
  bool alternate_class_ = false;
};

using PortalClass = tess::movement::LegacyWeighted<PassableTag, CostTag>;
using OtherPortalClass =
    tess::movement::LegacyWeighted<PassableTag, OtherCostTag>;

// The portal segment cache is the third policy in three headers: an
// ENTRY budget (not bytes), sweep-then-evict-oldest rather than LRU, and
// stale entries that linger until a sweep reclaims them. A shared model
// across the three would assert something false for two of them.
class PortalSegmentCacheModel {
 public:
  static constexpr std::uint32_t kSegments = 5;
  static constexpr std::uint32_t kActions = 6;
  static constexpr std::uint32_t kOperationCount = kSegments * kActions;
  static constexpr std::size_t kBudget = 3;

  PortalSegmentCacheModel() {
    for (auto& page : world_.chunks()) {
      for (auto& tile : page.template field_span<PassableTag>()) {
        tile = 1;
      }
      for (auto& cost : page.template field_span<CostTag>()) {
        cost = 1;
      }
      for (auto& cost : page.template field_span<OtherCostTag>()) {
        cost = 1;
      }
    }
    scratch_.reserve_nodes(256);
    cache_.set_segment_budget(kBudget);
  }

  void apply(std::uint32_t op) {
    const auto segment = op % kSegments;
    switch (op / kSegments % kActions) {
      case 0:
      case 1:
        store(segment);
        break;
      case 2:
      case 3:
        lookup(segment);
        break;
      case 4:
        // A world edit makes every stored segment stale. They stay
        // resident until a sweep or a budget-triggered compaction
        // removes them, which is the interleaving worth randomizing.
        touch_world();
        break;
      default:
        cache_.sweep_stale(world_);
        break;
    }
  }

  [[nodiscard]] auto check() const -> std::optional<property::Violation> {
    const auto stats = cache_.stats();
    if (cache_.size() > cache_.segment_budget()) {
      return violation("entries stay within the segment budget", cache_.size(),
                       cache_.segment_budget());
    }
    if (stats.entries != cache_.size()) {
      return violation("size() and the reported entry count agree",
                       stats.entries, cache_.size());
    }
    if (stats.entries == 0 && stats.path_nodes != 0) {
      return violation("an empty cache holds no path storage", stats.path_nodes,
                       0);
    }
    if (miss_touched_output_) {
      return violation("a miss or stale entry leaves the output untouched", 1,
                       0);
    }
    if (duplicate_grew_) {
      return violation("re-storing a live request does not add a second entry",
                       1, 0);
    }
    return std::nullopt;
  }

  [[nodiscard]] auto sweeps() const -> std::size_t {
    return cache_.stats().sweeps;
  }
  [[nodiscard]] auto evictions() const -> std::size_t {
    return cache_.stats().evictions;
  }
  [[nodiscard]] auto hits() const -> std::size_t { return hits_; }
  [[nodiscard]] auto misses_with_entries() const -> std::size_t {
    return misses_with_entries_;
  }
  [[nodiscard]] auto live_restores() const -> std::size_t {
    return live_restores_;
  }

 private:
  static auto violation(const char* name, std::uint64_t observed,
                        std::uint64_t expected)
      -> std::optional<property::Violation> {
    std::ostringstream detail;
    detail << "observed " << observed << ", expected " << expected;
    return property::Violation{name, detail.str(), 0};
  }

  [[nodiscard]] static auto request_for(std::uint32_t segment)
      -> tess::PathRequest {
    const auto y = static_cast<std::int64_t>(segment);
    return tess::PathRequest{tess::Coord3{0, y, 0}, tess::Coord3{5, y, 0}};
  }

  void store(std::uint32_t segment) {
    const auto request = request_for(segment);
    const auto result =
        tess::weighted_astar_path<CacheWorld, PassableTag, CostTag>(
            world_, request, scratch_);
    if (result.status != tess::PathStatus::Found) {
      return;
    }
    const auto before = cache_.size();
    // Whether this request is already cached AND still valid decides
    // whether a re-store is a no-op. A stale match is skipped without
    // being erased, so below budget a re-store legitimately appends a
    // duplicate -- the idempotence claim holds only for live entries.
    std::vector<tess::Coord3> probe;
    auto view = cache_.for_class<PortalClass>();
    const auto live = view.lookup_append(world_, request, probe).found;

    auto store_view = cache_.for_class<PortalClass>();
    store_view.store(world_, request, result);
    if (live && cache_.size() > before) {
      duplicate_grew_ = true;
    }
    if (live) {
      ++live_restores_;
    }
  }

  void lookup(std::uint32_t segment) {
    std::vector<tess::Coord3> out;
    out.push_back(tess::Coord3{99, 99, 0});
    const auto marker = out.size();
    const auto entries_before = cache_.size();

    auto view = cache_.for_class<PortalClass>();
    const auto hit = view.lookup_append(world_, request_for(segment), out);
    if (hit.found) {
      ++hits_;
      return;
    }
    // A miss or a stale entry must leave the caller's buffer exactly as
    // it was.
    if (out.size() != marker) {
      miss_touched_output_ = true;
    }
    if (entries_before != 0) {
      ++misses_with_entries_;
    }
  }

  void touch_world() {
    world_.mark_dirty(
        tess::ChunkKey{0}, ++world_version_,
        tess::Box3{tess::Coord3{0, 0, 0}, tess::Extent3{1, 1, 1}});
  }

  CacheWorld world_{};
  tess::PathScratch scratch_{};
  tess::WeightedPortalSegmentCache cache_{};
  std::size_t hits_ = 0;
  std::size_t misses_with_entries_ = 0;
  std::size_t live_restores_ = 0;
  std::uint32_t world_version_ = 0;
  bool miss_touched_output_ = false;
  bool duplicate_grew_ = false;
};

constexpr std::size_t kSteps = 48;
constexpr std::uint64_t kSeeds = 24;

TEST(TessCacheProperty, FieldProductInvariantsHoldUnderRandomSequences) {
  const property::Property<FieldProductCacheModel> prop(
      property::current_test_name(), FieldProductCacheModel::kOperationCount);

  const auto request = property::replay_from_environment(
      FieldProductCacheModel::kOperationCount);
  if (request.present) {
    if (!request.error.empty()) {
      FAIL() << request.error;
    }
    const auto violation = prop.replay(request.sequence);
    EXPECT_FALSE(violation.has_value())
        << "replayed sequence still fails: "
        << (violation ? violation->invariant : "");
    return;
  }

  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    const auto failing = prop.run(seed, kSteps);
    if (failing.has_value()) {
      FAIL() << "seed " << seed << "\n" << prop.report(*failing);
    }
  }
}

TEST(TessCacheProperty, TheFieldProductSweepReachesEveryCachePath) {
  // Without these the bounds above hold over a cache that never filled,
  // never went stale, and never refused anything.
  const property::Property<FieldProductCacheModel> prop(
      property::current_test_name(), FieldProductCacheModel::kOperationCount);

  std::size_t evictions = 0;
  std::size_t stale = 0;
  std::size_t oversized = 0;
  std::size_t with_residents = 0;
  std::size_t replacements = 0;
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    FieldProductCacheModel model;
    for (const auto op : prop.sequence_for(seed, kSteps)) {
      model.apply(op);
    }
    evictions += model.evictions();
    stale += model.stale_rejections();
    oversized += model.oversized_rejections();
    with_residents += model.rejections_with_residents();
    replacements += model.replacements();
  }

  EXPECT_GT(evictions, 0U)
      << "the sweep never evicted, so the budget bound was never enforced";
  EXPECT_GT(stale, 0U)
      << "the sweep never went stale, so the stale-is-not-a-miss rule was "
         "never exercised";
  EXPECT_GT(oversized, 0U) << "the sweep never refused an over-budget store";
  EXPECT_GT(with_residents, 0U)
      << "every over-budget refusal happened against an EMPTY cache, so the "
         "rule that a rejected store preserves existing entries was never "
         "actually tested";
  EXPECT_GT(replacements, 0U)
      << "the sweep never replaced an existing key in place";
}

TEST(TessCacheProperty, ALookupRefreshesRecencyAndSavesAnEntry) {
  // The discriminating case, constructed directly rather than hoped for.
  // Fill to budget, then look up the OLDEST entry so it becomes the
  // most recent, then store a new key to force one eviction. Under LRU
  // the refreshed entry survives and the next-oldest goes; under FIFO
  // or insertion-order eviction the refreshed entry is the one that
  // leaves. A random sweep reaches this state only by luck -- verified
  // by mutation, which the sweep alone did not catch.
  FieldProductCacheModel model;
  for (std::uint32_t key = 0; key < FieldProductCacheModel::kBudgetProducts;
       ++key) {
    model.store_key(key);
  }
  ASSERT_EQ(model.entries(), FieldProductCacheModel::kBudgetProducts);

  // Key 0 is the least recently used; this makes it the most recent.
  EXPECT_TRUE(model.lookup_hits(0));

  // One more key than the budget affords, so exactly one entry goes.
  const auto newcomer =
      static_cast<std::uint32_t>(FieldProductCacheModel::kBudgetProducts);
  model.store_key(newcomer);
  EXPECT_EQ(model.entries(), FieldProductCacheModel::kBudgetProducts);

  EXPECT_TRUE(model.lookup_hits(0))
      << "the refreshed entry was evicted, so eviction is not "
         "least-recently-used";
  EXPECT_FALSE(model.lookup_hits(1))
      << "the least-recently-used entry survived";
  EXPECT_TRUE(model.lookup_hits(newcomer));
}

TEST(TessCacheProperty, TheModelsResidencyPredictionMatchesTheCache) {
  // Probing every key at the end of a sequence recovers the resident
  // set: a lookup refreshes recency but never changes membership, so
  // comparing the recovered set against the model's prediction is sound
  // even though probing mid-sequence would not be.
  //
  // What this does NOT do is verify the eviction POLICY, and the name
  // says so deliberately. Mutation testing showed a model predicting
  // FIFO instead of LRU passes this test unchanged: the sweep produces
  // only a handful of cache hits per seed, so the state where the two
  // policies disagree is essentially never reached, and rebalancing the
  // action weights and shrinking the key space did not change that.
  // ALookupRefreshesRecencyAndSavesAnEntry constructs that state
  // directly and is the test that owns the LRU claim.
  //
  // The value here is breadth: entry counts, byte totals and membership
  // stay consistent with a from-scratch model across long random
  // interleavings of store, lookup, staleness and clear.
  const property::Property<FieldProductCacheModel> prop(
      property::current_test_name(), FieldProductCacheModel::kOperationCount);

  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    FieldProductCacheModel model;
    for (const auto op : prop.sequence_for(seed, kSteps)) {
      model.apply(op);
    }
    auto expected = model.expected_probe();
    auto actual = model.probe_resident();
    std::sort(expected.begin(), expected.end());
    std::sort(actual.begin(), actual.end());
    EXPECT_EQ(actual, expected)
        << "seed " << seed
        << ": the cache holds a different set of keys than the model "
           "predicts";
  }
}

TEST(TessCacheProperty, RouteCacheInvariantsHoldUnderRandomSequences) {
  const property::Property<RouteCacheModel> prop(
      property::current_test_name(), RouteCacheModel::kOperationCount);

  const auto request =
      property::replay_from_environment(RouteCacheModel::kOperationCount);
  if (request.present) {
    if (!request.error.empty()) {
      FAIL() << request.error;
    }
    const auto violation = prop.replay(request.sequence);
    EXPECT_FALSE(violation.has_value())
        << "replayed sequence still fails: "
        << (violation ? violation->invariant : "");
    return;
  }

  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    const auto failing = prop.run(seed, kSteps);
    if (failing.has_value()) {
      FAIL() << "seed " << seed << "\n" << prop.report(*failing);
    }
  }
}

TEST(TessCacheProperty, TheRouteSweepReachesEveryCachePath) {
  const property::Property<RouteCacheModel> prop(
      property::current_test_name(), RouteCacheModel::kOperationCount);

  std::size_t skips = 0;
  std::size_t skips_with_residents = 0;
  std::size_t cap_invalidations = 0;
  std::size_t rebinds = 0;
  std::size_t hits = 0;
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    RouteCacheModel model;
    for (const auto op : prop.sequence_for(seed, kSteps)) {
      model.apply(op);
    }
    skips += model.oversized_skips();
    skips_with_residents += model.skips_with_residents();
    cap_invalidations += model.cap_invalidations();
    rebinds += model.class_rebinds();
    hits += model.hits() + model.suffix_hits();
  }

  EXPECT_GT(hits, 0U) << "the sweep never served a route from cache";
  EXPECT_GT(skips, 0U) << "the sweep never skipped an oversized route";
  EXPECT_GT(skips_with_residents, 0U)
      << "every oversized skip happened against an EMPTY cache, so the rule "
         "that a skip leaves resident entries alone was never tested";
  EXPECT_GT(cap_invalidations, 0U)
      << "the sweep never breached a cap, so the wholesale-invalidation "
         "policy was never exercised";
  EXPECT_GT(rebinds, 0U)
      << "the sweep never rebound the movement class, so the drop-on-rebind "
         "rule was never exercised";
}

TEST(TessCacheProperty, PortalSegmentInvariantsHoldUnderRandomSequences) {
  const property::Property<PortalSegmentCacheModel> prop(
      property::current_test_name(), PortalSegmentCacheModel::kOperationCount);

  const auto request = property::replay_from_environment(
      PortalSegmentCacheModel::kOperationCount);
  if (request.present) {
    if (!request.error.empty()) {
      FAIL() << request.error;
    }
    const auto violation = prop.replay(request.sequence);
    EXPECT_FALSE(violation.has_value())
        << "replayed sequence still fails: "
        << (violation ? violation->invariant : "");
    return;
  }

  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    const auto failing = prop.run(seed, kSteps);
    if (failing.has_value()) {
      FAIL() << "seed " << seed << "\n" << prop.report(*failing);
    }
  }
}

TEST(TessCacheProperty, ThePortalSweepReachesEveryCachePath) {
  const property::Property<PortalSegmentCacheModel> prop(
      property::current_test_name(), PortalSegmentCacheModel::kOperationCount);

  std::size_t sweeps = 0;
  std::size_t evictions = 0;
  std::size_t hits = 0;
  std::size_t misses_with_entries = 0;
  std::size_t live_restores = 0;
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    PortalSegmentCacheModel model;
    for (const auto op : prop.sequence_for(seed, kSteps)) {
      model.apply(op);
    }
    sweeps += model.sweeps();
    evictions += model.evictions();
    hits += model.hits();
    misses_with_entries += model.misses_with_entries();
    live_restores += model.live_restores();
  }

  EXPECT_GT(hits, 0U) << "the sweep never served a segment";
  EXPECT_GT(sweeps, 0U) << "the sweep never compacted";
  EXPECT_GT(evictions, 0U)
      << "the sweep never evicted, so the budget bound was never enforced";
  EXPECT_GT(misses_with_entries, 0U)
      << "every miss happened against an EMPTY cache, so the rule that a miss "
         "leaves the output untouched was never tested with entries present";
  EXPECT_GT(live_restores, 0U)
      << "no request was ever re-stored while still live, so the idempotence "
         "rule was never exercised";
}

}  // namespace
