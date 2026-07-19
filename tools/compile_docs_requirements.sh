#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

if (( $# > 1 )); then
  echo "usage: tools/compile_docs_requirements.sh [output-file]" >&2
  exit 2
fi

uv_version="$(uv --version)"
case "$uv_version" in
  "uv 0.11.29" | "uv 0.11.29 "*) ;;
  *)
    echo "error: uv 0.11.29 is required; found '$uv_version'" >&2
    exit 1
    ;;
esac

output="${1:-requirements-docs.txt}"
uv pip compile \
  --universal \
  --python-version 3.10 \
  --upgrade \
  --generate-hashes \
  --exclude-newer 2026-07-18T00:00:00Z \
  --custom-compile-command tools/compile_docs_requirements.sh \
  requirements-docs.in \
  -o "$output"
