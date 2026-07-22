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
cp "$root/docs/assets/tess-logo-dark.svg" "$output/logo.svg"

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

# Colony demo: colony_2d in the browser, published under $output/colony/.
colony="$output/colony"
mkdir -p "$colony"
cp "$root/examples/web_colony/site/index.html" "$colony/"
cp "$root/examples/web_colony/site/style.css" "$colony/"
cp "$root/examples/web_colony/site/app.js" "$colony/"
cp "$root/examples/web_colony/site/favicon.svg" "$colony/"
cp "$root/docs/assets/tess-logo-dark.svg" "$colony/logo.svg"

colony_exports='["_main","_tess_colony_width","_tess_colony_height"'
colony_exports+=',"_tess_colony_reset","_tess_colony_set_wall"'
colony_exports+=',"_tess_colony_set_strategy","_tess_colony_tick"'
colony_exports+=',"_tess_colony_tiles","_tess_colony_agents"'
colony_exports+=',"_tess_colony_agent_count","_tess_colony_arrived"]'

em++ \
  -std=c++20 \
  -O3 \
  -DNDEBUG \
  -I"$root/include" \
  -I"$config/generated/include" \
  "$root/examples/web_colony/colony.cc" \
  -sALLOW_MEMORY_GROWTH=1 \
  -sENVIRONMENT=web \
  -sFILESYSTEM=0 \
  -sMODULARIZE=1 \
  -sEXPORT_NAME=createTessColony \
  -sEXPORTED_FUNCTIONS="$colony_exports" \
  -sEXPORTED_RUNTIME_METHODS='["cwrap","HEAPU8","HEAP16"]' \
  -o "$colony/tess-colony.js"
