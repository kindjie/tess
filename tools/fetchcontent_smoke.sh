#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "$root/build"
work="$(mktemp -d "$root/build/tess-fetchcontent-smoke.XXXXXX")"
trap 'rm -rf "$work"' EXIT

cmake -S "$root/tests/fetchcontent_consumer" -B "$work" \
  -DTESS_SOURCE_DIR="$root"
cmake --build "$work" --parallel
"$work/tess_fetchcontent_consumer"
