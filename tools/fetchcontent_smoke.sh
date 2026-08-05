#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
no_exceptions="${TESS_NO_EXCEPTIONS:-0}"
# Set TESS_FETCHCONTENT_SMOKE_CONFIG for multi-config generators.
config="${TESS_FETCHCONTENT_SMOKE_CONFIG:-}"
mkdir -p "$root/build"
work="$(mktemp -d "$root/build/tess-fetchcontent-smoke.XXXXXX")"
trap 'rm -rf "$work"' EXIT

cmake -S "$root/tests/fetchcontent_consumer" -B "$work" \
  -DTESS_SOURCE_DIR="$root" \
  -DTESS_NO_EXCEPTIONS="$no_exceptions"
if [[ -n "$config" ]]; then
  cmake --build "$work" --config "$config" --parallel
  "$work/$config/tess_fetchcontent_consumer"
else
  cmake --build "$work" --parallel
  "$work/tess_fetchcontent_consumer"
fi
