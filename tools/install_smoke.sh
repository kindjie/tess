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
mkdir -p "$root/build"
work="$(mktemp -d "$root/build/tess-install-smoke.XXXXXX")"
trap 'rm -rf "$work"' EXIT
prefix="$work/prefix"
build="$work/build"

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

cmake -S "$root/tests/install_consumer" -B "$build" \
  -DCMAKE_PREFIX_PATH="$prefix" \
  -DTESS_EXPECTED_VERSION="$version"
if [[ -n "$config" ]]; then
  cmake --build "$build" --config "$config"
  "$build/$config/tess_install_consumer"
else
  cmake --build "$build"
  "$build/tess_install_consumer"
fi
