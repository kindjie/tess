#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${TESS_INSTALL_SMOKE_BUILD_DIR:-$root/build/dev}"
# Set TESS_INSTALL_SMOKE_CONFIG (e.g. Debug) for multi-config generators
# such as Visual Studio; single-config builds should leave it unset.
config="${TESS_INSTALL_SMOKE_CONFIG:-}"

if [[ ! -d "$build_dir" ]]; then
  echo "error: build directory '$build_dir' does not exist;" \
    "configure and build the dev preset first" >&2
  exit 1
fi

# Derive the expected version from the single authoritative version file.
version="$(sed -n 's/^set(TESS_VERSION \([0-9][0-9.]*\))$/\1/p' \
  "$root/cmake/tess-version.cmake")"
if [[ ! "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "error: could not parse TESS_VERSION" >&2
  exit 1
fi
major="${version%%.*}"
rest="${version#*.}"
minor="${rest%%.*}"
patch="${rest#*.}"

mkdir -p "$root/build"
work="$(mktemp -d "$root/build/tess-install-smoke.XXXXXX")"
trap 'rm -rf "$work"' EXIT
prefix="$work/prefix"
consumer="$work/consumer"
build="$work/build"

mkdir -p "$consumer"

# Bash 3.2 (macOS /bin/bash) treats empty-array expansion as an unbound
# variable under `set -u`, so branch instead of building argument arrays.
if [[ -n "$config" ]]; then
  cmake --install "$build_dir" --prefix "$prefix" --config "$config"
else
  cmake --install "$build_dir" --prefix "$prefix"
fi

if [[ ! -f "$prefix/share/licenses/tess/LICENSE" ]]; then
  echo "error: installed package is missing the MIT license" >&2
  exit 1
fi

cat > "$consumer/CMakeLists.txt" <<EOF
cmake_minimum_required(VERSION 3.25)
project(tess_install_smoke LANGUAGES CXX)
find_package(tess ${version} EXACT CONFIG REQUIRED)
if(NOT tess_VERSION VERSION_EQUAL "${version}")
  message(FATAL_ERROR "package version does not match ${version}")
endif()
add_executable(tess_install_smoke main.cc)
target_link_libraries(tess_install_smoke PRIVATE tess::tess)
EOF

cat > "$consumer/main.cc" <<EOF
#include <tess/tess.h>

int main() {
  static_assert(TESS_VERSION_MAJOR == ${major});
  static_assert(TESS_VERSION_MINOR == ${minor});
  static_assert(TESS_VERSION_PATCH == ${patch});
  static_assert(tess::library_version.major == ${major});
  static_assert(tess::library_version.minor == ${minor});
  static_assert(tess::library_version.patch == ${patch});
  return 0;
}
EOF

cmake -S "$consumer" -B "$build" -DCMAKE_PREFIX_PATH="$prefix"
if [[ -n "$config" ]]; then
  cmake --build "$build" --config "$config"
  "$build/$config/tess_install_smoke"
else
  cmake --build "$build"
  "$build/tess_install_smoke"
fi
