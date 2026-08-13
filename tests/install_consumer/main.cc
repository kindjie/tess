#include <tess/pathfinding.h>
#include <tess/simulation.h>
#include <tess/tess.h>

int main() {
#ifdef TESS_EXPECT_NO_EXCEPTIONS
  static_assert(TESS_HAS_EXCEPTIONS == 0);
  static_assert(!tess::has_exceptions);
#endif
  static_assert(TESS_VERSION_MAJOR == TESS_EXPECTED_VERSION_MAJOR);
  static_assert(TESS_VERSION_MINOR == TESS_EXPECTED_VERSION_MINOR);
  static_assert(TESS_VERSION_PATCH == TESS_EXPECTED_VERSION_PATCH);
  static_assert(tess::library_version.major == TESS_EXPECTED_VERSION_MAJOR);
  static_assert(tess::library_version.minor == TESS_EXPECTED_VERSION_MINOR);
  static_assert(tess::library_version.patch == TESS_EXPECTED_VERSION_PATCH);
  static_assert(tess::library_version.prerelease ==
                TESS_EXPECTED_VERSION_PRERELEASE);
  static_assert(std::string_view{TESS_VERSION_PRERELEASE} ==
                TESS_EXPECTED_VERSION_PRERELEASE);
  static_assert(std::string_view{TESS_VERSION_STRING} ==
                TESS_EXPECTED_VERSION_STRING);
  return 0;
}
