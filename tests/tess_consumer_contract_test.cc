#include <gtest/gtest.h>

#include <cstdint>

#include "consumer_contract/contract.h"
#include "tess/core/config.h"

#if defined(TESS_EXPECT_NO_EXCEPTIONS)
static_assert(!tess::has_exceptions);
#endif

namespace {

namespace contract = tess_test::contract;

// These tests exist for what they LINK as much as what they assert:
// the probe translation units each include the whole public header set
// in a different order, so a header that lost an inline, gained a
// non-inline namespace-scope definition, or silently depended on a
// sibling include fails the build before any assertion runs.

TEST(TessConsumerContract, TagIdentityIsStableAcrossTranslationUnits) {
  // tag_identity() hands back the address of a static local in an
  // inline function template, so every translation unit in a program
  // must observe the same object. A mismatch means the facility
  // acquired per-translation-unit state — invisible to a single-TU
  // test, and corrupting for anything using it as a cache key.
  EXPECT_EQ(contract::tag_identity_forward(), contract::tag_identity_reverse());
  EXPECT_NE(contract::tag_identity_forward(), 0u);
}

TEST(TessConsumerContract, WorldStampIsStableAcrossTranslationUnits) {
  EXPECT_EQ(contract::world_stamp_forward(), contract::world_stamp_reverse());
  EXPECT_NE(contract::world_stamp_forward(), nullptr);
}

TEST(TessConsumerContract, LeafFirstConsumerWorks) {
  // Compiled from a translation unit that includes only the narrowest
  // owning headers and no umbrella, which is what the packaging guide
  // recommends consumers do.
  EXPECT_TRUE(contract::leaf_first_world_is_usable());
}

}  // namespace
