#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
output="${1:-$root/build/web-demo}"
config="$root/build/web-demo-config"

mkdir -p "$output"
cp "$root/examples/web_pathfinder/site/index.html" "$output/"
cp "$root/examples/web_pathfinder/site/style.css" "$output/"
cp "$root/examples/web_pathfinder/site/app.js" "$output/"
cp "$root/examples/web_pathfinder/site/favicon.svg" "$output/"

cmake \
  -S "$root" \
  -B "$config" \
  -DTESS_BUILD_TESTING=OFF \
  -DTESS_BUILD_EXAMPLES=OFF \
  -DTESS_BUILD_BENCHMARKS=OFF \
  -DTESS_BUILD_DOCS=OFF \
  -DTESS_ENABLE_ENTT=OFF

exported_functions='["_main","_tess_demo_width","_tess_demo_height"'
exported_functions+=',"_tess_demo_reset","_tess_demo_set_blocked"'
exported_functions+=',"_tess_demo_find_path","_tess_demo_path_x"'
exported_functions+=',"_tess_demo_path_y"]'

em++ \
  -std=c++20 \
  -O3 \
  -DNDEBUG \
  -I"$root/include" \
  -I"$config/generated/include" \
  "$root/examples/web_pathfinder/pathfinder.cc" \
  -sALLOW_MEMORY_GROWTH=1 \
  -sENVIRONMENT=web \
  -sFILESYSTEM=0 \
  -sMODULARIZE=1 \
  -sEXPORT_NAME=createTessDemo \
  -sEXPORTED_FUNCTIONS="$exported_functions" \
  -sEXPORTED_RUNTIME_METHODS='["cwrap"]' \
  -o "$output/tess-demo.js"
