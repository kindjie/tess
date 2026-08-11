#include <tess/tess.h>

int main() {
  static_assert(tess::library_version.major == TESS_VERSION_MAJOR);
  return tess::library_version.major < 0 ? 1 : 0;
}
