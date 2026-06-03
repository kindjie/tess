#include <gtest/gtest.h>

#include <tess/tess.hpp>

TEST(TessSmoke, ExposesLibraryVersion) {
  static_assert(tess::library_version.major == TESS_VERSION_MAJOR);
  static_assert(tess::library_version.minor == TESS_VERSION_MINOR);
  static_assert(tess::library_version.patch == TESS_VERSION_PATCH);

  EXPECT_EQ(tess::library_version.major, TESS_VERSION_MAJOR);
  EXPECT_EQ(tess::library_version.minor, TESS_VERSION_MINOR);
  EXPECT_EQ(tess::library_version.patch, TESS_VERSION_PATCH);
}
