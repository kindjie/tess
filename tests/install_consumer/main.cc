#include <tess/pathfinding.h>
#include <tess/simulation.h>

int main() {
  static_assert(TESS_VERSION_MAJOR == TESS_EXPECTED_VERSION_MAJOR);
  static_assert(TESS_VERSION_MINOR == TESS_EXPECTED_VERSION_MINOR);
  static_assert(TESS_VERSION_PATCH == TESS_EXPECTED_VERSION_PATCH);
  static_assert(tess::library_version.major == TESS_EXPECTED_VERSION_MAJOR);
  static_assert(tess::library_version.minor == TESS_EXPECTED_VERSION_MINOR);
  static_assert(tess::library_version.patch == TESS_EXPECTED_VERSION_PATCH);
  return 0;
}
