#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
work="${TMPDIR:-/tmp}/tess-install-smoke"
prefix="$work/prefix"
consumer="$work/consumer"
build="$work/build"

rm -rf "$work"
mkdir -p "$consumer"

cmake --install "$root/build/dev" --prefix "$prefix"

cat > "$consumer/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.28)
project(tess_install_smoke LANGUAGES CXX)
find_package(tess CONFIG REQUIRED)
add_executable(tess_install_smoke main.cc)
target_link_libraries(tess_install_smoke PRIVATE tess::tess)
EOF

cat > "$consumer/main.cc" <<'EOF'
#include <tess/tess.h>

int main() {
  static_assert(TESS_VERSION_MAJOR == 0);
  return tess::library_version.minor == 1 ? 0 : 1;
}
EOF

cmake -S "$consumer" -B "$build" -DCMAKE_PREFIX_PATH="$prefix"
cmake --build "$build"
"$build/tess_install_smoke"
