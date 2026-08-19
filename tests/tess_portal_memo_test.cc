// Unit tests for the request-scoped portal memo. The memo's key omits
// the goal, the world and the movement class, so the tests here exist
// mostly to pin the two mechanisms that make that omission sound: the
// generation stamp and the selection scope.

#include <gtest/gtest.h>
#include <tess/path/detail/portal_memo.h>

#include <array>
#include <cstdint>
#include <thread>

namespace {

using tess::detail::portal_step_code;
using tess::detail::PortalMemo;
using tess::detail::PortalMemoScope;

TEST(TessPortalMemo, StepCodeDistinguishesSignedDirections) {
  const auto origin = tess::ChunkCoord3{4, 4, 4};
  EXPECT_EQ(portal_step_code(origin, tess::ChunkCoord3{5, 4, 4}), 1);
  EXPECT_EQ(portal_step_code(origin, tess::ChunkCoord3{3, 4, 4}), -1);
  EXPECT_EQ(portal_step_code(origin, tess::ChunkCoord3{4, 5, 4}), 2);
  EXPECT_EQ(portal_step_code(origin, tess::ChunkCoord3{4, 3, 4}), -2);
  EXPECT_EQ(portal_step_code(origin, tess::ChunkCoord3{4, 4, 5}), 3);
  EXPECT_EQ(portal_step_code(origin, tess::ChunkCoord3{4, 4, 3}), -3);
  EXPECT_EQ(portal_step_code(origin, origin), 0);
}

TEST(TessPortalMemo, StoresAndServesWithinOneSelection) {
  PortalMemo memo;
  const PortalMemoScope scope{memo};

  const auto miss = memo.probe(42, 1);
  EXPECT_FALSE(miss.hit);
  EXPECT_TRUE(miss.insertable);
  memo.store(miss, 42, 1, 4242, true);

  const auto hit = memo.probe(42, 1);
  EXPECT_TRUE(hit.hit);
  EXPECT_TRUE(hit.found);
  EXPECT_EQ(hit.portal_index, 4242u);
  EXPECT_EQ(memo.hits(), 1u);
}

TEST(TessPortalMemo, ServesCachedFailuresWithoutAPortal) {
  PortalMemo memo;
  const PortalMemoScope scope{memo};

  const auto miss = memo.probe(7, -2);
  memo.store(miss, 7, -2, 0, false);

  const auto hit = memo.probe(7, -2);
  EXPECT_TRUE(hit.hit);
  EXPECT_FALSE(hit.found);
}

TEST(TessPortalMemo, DirectionIsPartOfTheKey) {
  PortalMemo memo;
  const PortalMemoScope scope{memo};

  const auto miss = memo.probe(9, 1);
  memo.store(miss, 9, 1, 111, true);

  // Same tile, opposite direction: a different query, and serving the
  // stored answer would be wrong.
  EXPECT_FALSE(memo.probe(9, -1).hit);
}

TEST(TessPortalMemo, ANewSelectionRetiresEveryEntry) {
  PortalMemo memo;
  {
    const PortalMemoScope scope{memo};
    const auto miss = memo.probe(5, 1);
    memo.store(miss, 5, 1, 500, true);
    ASSERT_TRUE(memo.probe(5, 1).hit);
  }
  // The next selection carries a different goal, so the poisoned entry
  // must not be served even though its packed key is identical.
  const PortalMemoScope scope{memo};
  EXPECT_FALSE(memo.probe(5, 1).hit);
}

TEST(TessPortalMemo, NestedSelectionBypassesRatherThanSharing) {
  PortalMemo memo;
  const PortalMemoScope outer{memo};
  const auto miss = memo.probe(11, 1);
  memo.store(miss, 11, 1, 1100, true);
  ASSERT_TRUE(memo.probe(11, 1).hit);

  {
    const PortalMemoScope nested{memo};
    EXPECT_TRUE(memo.bypassed());
    const auto nested_probe = memo.probe(11, 1);
    EXPECT_FALSE(nested_probe.hit);
    EXPECT_FALSE(nested_probe.insertable);
  }

  // The outer selection's entries survived the nested one.
  EXPECT_FALSE(memo.bypassed());
  EXPECT_TRUE(memo.probe(11, 1).hit);
}

TEST(TessPortalMemo, ScopeRestoresStateWhenAnExceptionUnwinds) {
  PortalMemo memo;
  const PortalMemoScope outer{memo};
  const auto miss = memo.probe(13, 2);
  memo.store(miss, 13, 2, 1300, true);

  try {
    const PortalMemoScope nested{memo};
    throw std::runtime_error{"selection failed"};
  } catch (const std::runtime_error&) {  // NOLINT(bugprone-empty-catch)
  }

  EXPECT_FALSE(memo.bypassed());
  EXPECT_TRUE(memo.active());
  EXPECT_TRUE(memo.probe(13, 2).hit);
}

TEST(TessPortalMemo, SaturationFallsBackAndStopsProbing) {
  PortalMemo memo;
  const PortalMemoScope scope{memo};

  for (std::uint64_t i = 0; i < PortalMemo::kCapacity; ++i) {
    const auto miss = memo.probe(i, 1);
    ASSERT_TRUE(miss.insertable) << "entry " << i;
    memo.store(miss, i, 1, i + 1000, true);
  }
  EXPECT_FALSE(memo.saturated());

  // One entry beyond capacity: the probe walks the table, gives up, and
  // reports a non-insertable miss so the caller does the original work.
  const auto overflow = memo.probe(PortalMemo::kCapacity, 1);
  EXPECT_FALSE(overflow.hit);
  EXPECT_FALSE(overflow.insertable);
  EXPECT_TRUE(memo.saturated());
  EXPECT_EQ(memo.saturations(), 1u);

  // Sticky: later lookups must not each pay another full walk, and a
  // previously stored key is no longer served once bypassed.
  const auto after = memo.probe(0, 1);
  EXPECT_FALSE(after.hit);
  EXPECT_EQ(memo.saturations(), 1u);
}

TEST(TessPortalMemo, EachThreadHasItsOwnMemo) {
  auto& mine = tess::detail::active_portal_memo();
  const PortalMemoScope scope{mine};
  const auto miss = mine.probe(21, 1);
  mine.store(miss, 21, 1, 2100, true);
  ASSERT_TRUE(mine.probe(21, 1).hit);

  auto other_saw_entry = true;
  std::thread worker{[&] {
    auto& theirs = tess::detail::active_portal_memo();
    const PortalMemoScope other_scope{theirs};
    other_saw_entry = theirs.probe(21, 1).hit;
  }};
  worker.join();
  EXPECT_FALSE(other_saw_entry)
      << "a shared memo would leak entries keyed for another goal";
}

}  // namespace
