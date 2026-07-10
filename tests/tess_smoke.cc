#include <gtest/gtest.h>
#include <tess/tess.h>

TEST(TessSmoke, ExposesLibraryVersion) {
  static_assert(tess::library_version.major == TESS_VERSION_MAJOR);
  static_assert(tess::library_version.minor == TESS_VERSION_MINOR);
  static_assert(tess::library_version.patch == TESS_VERSION_PATCH);

  // Pin the actual released version. Comparing the macros only to
  // themselves (or to library_version, which is built from them) would be a
  // tautology. The literals below are a hand-maintained mirror of the
  // release version (`project(... VERSION 0.1.0)` in the top-level
  // CMakeLists.txt, which the build does not inject here): a tess.h macro
  // bump that forgets this test fails here, prompting the CMake check; a
  // CMakeLists-only bump is NOT detectable by this test.
  static_assert(TESS_VERSION_MAJOR == 0);
  static_assert(TESS_VERSION_MINOR == 1);
  static_assert(TESS_VERSION_PATCH == 0);

  EXPECT_EQ(tess::library_version.major, 0);
  EXPECT_EQ(tess::library_version.minor, 1);
  EXPECT_EQ(tess::library_version.patch, 0);
}
