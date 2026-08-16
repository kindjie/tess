#include <tess/tess.h>

#include <string_view>

int main() {
  static_assert(tess::library_version.major == TESS_VERSION_MAJOR);
  static_assert(tess::library_version.minor == TESS_VERSION_MINOR);
  static_assert(tess::library_version.patch == TESS_VERSION_PATCH);

  return std::string_view{TESS_VERSION_STRING}.empty() ? 1 : 0;
}
