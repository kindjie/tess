#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${TESS_INSTALL_SMOKE_BUILD_DIR:-$root/build/dev}"

if [[ ! -d "$build_dir" ]]; then
  echo "error: build directory '$build_dir' does not exist;" \
    "configure and build the dev preset first" >&2
  exit 1
fi

# Derive the expected version from CMakeLists.txt so a version bump cannot
# silently diverge from this check.
version="$(sed -n 's/^ *VERSION \([0-9][0-9.]*\)$/\1/p' \
  "$root/CMakeLists.txt" | head -n 1)"
if [[ ! "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "error: could not parse project VERSION from CMakeLists.txt" >&2
  exit 1
fi
major="${version%%.*}"
rest="${version#*.}"
minor="${rest%%.*}"

work="$(mktemp -d "${TMPDIR:-/tmp}/tess-install-smoke.XXXXXX")"
trap 'rm -rf "$work"' EXIT
prefix="$work/prefix"
consumer="$work/consumer"
build="$work/build"

mkdir -p "$consumer"

cmake --install "$build_dir" --prefix "$prefix"

cat > "$consumer/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.28)
project(tess_install_smoke LANGUAGES CXX)
find_package(tess CONFIG REQUIRED)
add_executable(tess_install_smoke main.cc)
target_link_libraries(tess_install_smoke PRIVATE tess::tess)
EOF

cat > "$consumer/main.cc" <<EOF
#include <tess/tess.h>

int main() {
  static_assert(TESS_VERSION_MAJOR == ${major});
  return tess::library_version.minor == ${minor}u ? 0 : 1;
}
EOF

cmake -S "$consumer" -B "$build" -DCMAKE_PREFIX_PATH="$prefix"
cmake --build "$build"
"$build/tess_install_smoke"
