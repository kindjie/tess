#!/usr/bin/env bash
set -euo pipefail

if ! command -v ccache >/dev/null 2>&1; then
  echo "::notice::ccache was not installed; no compiler-cache stats available"
  exit 0
fi

ccache --show-stats
