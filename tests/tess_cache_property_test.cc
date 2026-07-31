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

using CacheShape =
    tess::Shape<tess::Extent3{32, 32, 1}, tess::Extent3{16, 16, 1}>;
using CacheSchema = tess::FieldSchema<tess::Field<PassableTag, std::uint8_t>>;
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

}  // namespace
