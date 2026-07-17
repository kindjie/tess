#include <gtest/gtest.h>
#include <tess/tess.h>

TEST(TessSmoke, ExposesLibraryVersion) {
  static_assert(tess::library_version.major == TESS_VERSION_MAJOR);
  static_assert(tess::library_version.minor == TESS_VERSION_MINOR);
  static_assert(tess::library_version.patch == TESS_VERSION_PATCH);

  static_assert(TESS_VERSION_MAJOR == TESS_EXPECTED_VERSION_MAJOR);
  static_assert(TESS_VERSION_MINOR == TESS_EXPECTED_VERSION_MINOR);
  static_assert(TESS_VERSION_PATCH == TESS_EXPECTED_VERSION_PATCH);

  EXPECT_EQ(tess::library_version.major, TESS_EXPECTED_VERSION_MAJOR);
  EXPECT_EQ(tess::library_version.minor, TESS_EXPECTED_VERSION_MINOR);
  EXPECT_EQ(tess::library_version.patch, TESS_EXPECTED_VERSION_PATCH);
}
