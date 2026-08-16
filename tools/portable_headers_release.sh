#!/usr/bin/env bash
set -euo pipefail

if [[ "$#" -ne 2 ]]; then
  echo "usage: $0 EXPECTED_VERSION TESTED_SHA" >&2
  exit 2
fi

expected_version="$1"
tested_sha="$2"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$root/build/portable-headers"
install="$root/build/portable-install"
assets="$root/build/portable-assets"
tar_tree="$root/build/portable-tar"
zip_tree="$root/build/portable-zip"

cmake -S "$root" -B "$build" \
  -DTESS_BUILD_TESTING=OFF \
  -DTESS_BUILD_EXAMPLES=OFF \
  -DTESS_BUILD_BENCHMARKS=OFF \
  -DTESS_BUILD_DOCS=OFF
cmake --install "$build" --prefix "$install"
python3 "$root/tools/package_portable_headers.py" \
  --install-prefix "$install" \
  --output-dir "$assets" \
  --source-sha "$tested_sha" \
  --expected-version "$expected_version"

(cd "$assets" && sha256sum --check --strict SHA256SUMS)
mkdir -p "$tar_tree" "$zip_tree"
tar --extract --gzip \
  --file "$assets/tess-$expected_version-headers.tar.gz" \
  --directory "$tar_tree"
python3 -m zipfile --extract \
  "$assets/tess-$expected_version-headers.zip" "$zip_tree"
diff --recursive --unified "$tar_tree" "$zip_tree"

include="$tar_tree/tess-$expected_version/include"
clang++-16 -std=c++20 -fno-exceptions -fno-rtti \
  -I"$include" "$root/tests/portable_headers_consumer.cc" \
  -o "$root/build/portable-clang-consumer"
g++-12 -std=c++20 -I"$include" \
  "$root/tests/portable_headers_consumer.cc" \
  -o "$root/build/portable-gcc-consumer"
"$root/build/portable-clang-consumer"
"$root/build/portable-gcc-consumer"
