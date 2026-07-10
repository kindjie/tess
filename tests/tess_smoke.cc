#include <gtest/gtest.h>
#include <tess/tess.h>

TEST(TessSmoke, ExposesLibraryVersion) {
  static_assert(tess::library_version.major == TESS_VERSION_MAJOR);
  static_assert(tess::library_version.minor == TESS_VERSION_MINOR);
  static_assert(tess::library_version.patch == TESS_VERSION_PATCH);

  // Pin the actual released version against an independent source: the
  // literals below mirror `project(... VERSION 0.1.0)` in the top-level
  // CMakeLists.txt. Comparing the macros only to themselves (or to
  // library_version, which is built from them) would be a tautology; this
  // fails if either tess.h or CMakeLists.txt is bumped without the other.
  static_assert(TESS_VERSION_MAJOR == 0);
  static_assert(TESS_VERSION_MINOR == 2);
  static_assert(TESS_VERSION_PATCH == 0);

  EXPECT_EQ(tess::library_version.major, 0);
  EXPECT_EQ(tess::library_version.minor, 2);
  EXPECT_EQ(tess::library_version.patch, 0);
}
