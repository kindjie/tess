#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
output="${1:-$root/build/web-demo}"
config="${TESS_WEB_DEMO_CONFIG:-$root/build/web-demo-config}"

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
  -DTESS_ENABLE_ENTT=OFF \
  -DTESS_ENABLE_FLECS=OFF

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

# Pathfinding strategy comparison: the native example's shared C++ model with
# a read-only browser adapter, published under $output/strategies/.
strategies="$output/strategies"
mkdir -p "$strategies"
cp "$root/examples/web_pathfinding_strategies/site/index.html" "$strategies/"
cp "$root/examples/web_pathfinding_strategies/site/style.css" "$strategies/"
cp "$root/examples/web_pathfinding_strategies/site/app.js" "$strategies/"
cp "$root/examples/web_pathfinder/site/favicon.svg" "$strategies/"

strategies_exports='["_main","_tess_strategies_readiness"'
strategies_exports+=',"_tess_strategies_width","_tess_strategies_height"'
strategies_exports+=',"_tess_strategies_count"'
strategies_exports+=',"_tess_strategies_cell_passable"'
strategies_exports+=',"_tess_strategies_request_count"'
strategies_exports+=',"_tess_strategies_path_status"'
strategies_exports+=',"_tess_strategies_path_cost"'
strategies_exports+=',"_tess_strategies_path_expansions"'
strategies_exports+=',"_tess_strategies_path_size"'
strategies_exports+=',"_tess_strategies_path_x"'
strategies_exports+=',"_tess_strategies_path_y"'
strategies_exports+=',"_tess_strategies_cache_hits"'
strategies_exports+=',"_tess_strategies_cache_misses"'
strategies_exports+=',"_tess_strategies_batch_unique_goals"'
strategies_exports+=',"_tess_strategies_batch_field_builds"'
strategies_exports+=',"_tess_strategies_batch_fallbacks"'
strategies_exports+=',"_tess_strategies_field_builds"'
strategies_exports+=',"_tess_strategies_field_expansions"'
strategies_exports+=',"_tess_strategies_field_reached_nodes"]'

em++ \
  -std=c++20 \
  -O3 \
  -DNDEBUG \
  -I"$root/include" \
  -I"$config/generated/include" \
  "$root/examples/pathfinding_strategies_model.cc" \
  "$root/examples/web_pathfinding_strategies/strategies_wasm.cc" \
  -sALLOW_MEMORY_GROWTH=1 \
  -sENVIRONMENT=web \
  -sFILESYSTEM=0 \
  -sMODULARIZE=1 \
  -sEXPORT_NAME=createTessStrategies \
  -sEXPORTED_FUNCTIONS="$strategies_exports" \
  -sEXPORTED_RUNTIME_METHODS='["cwrap"]' \
  -o "$strategies/tess-strategies.js"

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
colony_exports+=',"_tess_colony_set_spread"'
colony_exports+=',"_tess_colony_stalled_ticks"'
colony_exports+=',"_tess_colony_planning_pending"'
colony_exports+=',"_tess_colony_advanced_last_tick"'
colony_exports+=',"_tess_colony_movement_waits_last_tick"'
colony_exports+=',"_tess_colony_tiles","_tess_colony_agents"'
colony_exports+=',"_tess_colony_previous_agents"'
colony_exports+=',"_tess_colony_interpolation_alpha"'
colony_exports+=',"_tess_colony_agent_count","_tess_colony_arrived"'
colony_exports+=',"_tess_colony_unreachable","_tess_colony_crowd_blocked"'
colony_exports+=',"_tess_colony_turnaround_ready","_tess_colony_leg"'
colony_exports+=',"_tess_colony_completed_legs","_tess_colony_aborted_legs"'
colony_exports+=',"_tess_colony_relaunch"]'

em++ \
  -std=c++20 \
  -O3 \
  -DNDEBUG \
  -I"$root/include" \
  -I"$config/generated/include" \
  "$root/examples/web_colony/colony_model.cc" \
  "$root/examples/web_colony/colony_wasm.cc" \
  -sALLOW_MEMORY_GROWTH=1 \
  -sENVIRONMENT=web \
  -sFILESYSTEM=0 \
  -sMODULARIZE=1 \
  -sEXPORT_NAME=createTessColony \
  -sEXPORTED_FUNCTIONS="$colony_exports" \
  -sEXPORTED_RUNTIME_METHODS='["cwrap","HEAPU8","HEAP16"]' \
  -o "$colony/tess-colony.js"

# Traffic Lab: a separate large-grid specialization with static scenario
# terrain and 1,024 moving agents, published under $output/traffic/.
traffic="$output/traffic"
mkdir -p "$traffic"
cp "$root/examples/web_traffic/site/index.html" "$traffic/"
cp "$root/examples/web_traffic/site/style.css" "$traffic/"
cp "$root/examples/web_traffic/site/app.js" "$traffic/"
cp "$root/examples/web_colony/site/favicon.svg" "$traffic/"
cp "$root/docs/assets/tess-logo-dark.svg" "$traffic/logo.svg"

traffic_exports='["_main","_tess_traffic_width","_tess_traffic_height"'
traffic_exports+=',"_tess_traffic_agent_count","_tess_traffic_reset"'
traffic_exports+=',"_tess_traffic_tick","_tess_traffic_terrain"'
traffic_exports+=',"_tess_traffic_agents","_tess_traffic_previous_agents"'
traffic_exports+=',"_tess_traffic_interpolation_alpha"'
traffic_exports+=',"_tess_traffic_planning_us"'
traffic_exports+=',"_tess_traffic_planning_queries"'
traffic_exports+=',"_tess_traffic_fixed_ticks"'
traffic_exports+=',"_tess_traffic_planning_pending"'
traffic_exports+=',"_tess_traffic_advanced","_tess_traffic_waits"'
traffic_exports+=',"_tess_traffic_blocked","_tess_traffic_arrived"'
traffic_exports+=',"_tess_traffic_one_progress_streak"'
traffic_exports+=',"_tess_traffic_longest_one_progress_streak"]'

em++ \
  -std=c++20 \
  -O3 \
  -DNDEBUG \
  -I"$root/include" \
  -I"$config/generated/include" \
  "$root/examples/web_traffic/traffic_model.cc" \
  "$root/examples/web_traffic/traffic_wasm.cc" \
  -sALLOW_MEMORY_GROWTH=1 \
  -sENVIRONMENT=web \
  -sFILESYSTEM=0 \
  -sMODULARIZE=1 \
  -sEXPORT_NAME=createTessTraffic \
  -sEXPORTED_FUNCTIONS="$traffic_exports" \
  -sEXPORTED_RUNTIME_METHODS='["cwrap","HEAPU8","HEAP16"]' \
  -o "$traffic/tess-traffic.js"

# Reference diagnostics integration: tess remains dependency-free, while this
# one Pages artifact fetches the exact Dear ImGui revision that it compiles.
diagnostics="$output/diagnostics"
mkdir -p "$diagnostics"
cp "$root/examples/web_diagnostics/site/index.html" "$diagnostics/"
cp "$root/examples/web_diagnostics/site/style.css" "$diagnostics/"
cp "$root/examples/web_diagnostics/site/app.js" "$diagnostics/"
cp "$root/examples/web_pathfinder/site/favicon.svg" "$diagnostics/"
cp "$root/docs/assets/tess-logo-dark.svg" "$diagnostics/logo.svg"

imgui="$config/_deps/imgui-src"
git_executable="$(command -v git)"
cmake \
  -DTESS_GIT_EXECUTABLE="$git_executable" \
  -DTESS_GIT_DEPENDENCY=imgui \
  -DTESS_GIT_REPOSITORY=https://github.com/ocornut/imgui.git \
  -DTESS_GIT_REVISION=8936b58fe26e8c3da834b8f60b06511d537b4c63 \
  -DTESS_GIT_SOURCE_DIR="$imgui" \
  -P "$root/cmake/TessGitPopulate.cmake"
cp "$imgui/LICENSE.txt" "$diagnostics/third-party-imgui-LICENSE.txt"

diagnostics_exports='["_main","_tess_diagnostics_status"'
diagnostics_exports+=',"_tess_diagnostics_set_paused"'
diagnostics_exports+=',"_tess_diagnostics_set_intensity"'
diagnostics_exports+=',"_tess_diagnostics_select"'
diagnostics_exports+=',"_tess_diagnostics_set_passable"'
diagnostics_exports+=',"_tess_diagnostics_paused"'
diagnostics_exports+=',"_tess_diagnostics_intensity"'
diagnostics_exports+=',"_tess_diagnostics_selected_x"'
diagnostics_exports+=',"_tess_diagnostics_selected_y"'
diagnostics_exports+=',"_tess_diagnostics_selected_passable"]'

em++ \
  -std=c++20 \
  -O3 \
  -DNDEBUG \
  -DTESS_ENABLE_DIAGNOSTICS \
  -DTESS_ENABLE_IMGUI \
  -I"$root/include" \
  -I"$config/generated/include" \
  -I"$imgui" \
  -I"$imgui/backends" \
  "$root/examples/web_diagnostics/diagnostics_model.cc" \
  "$root/examples/web_diagnostics/diagnostics_wasm.cc" \
  "$imgui/imgui.cpp" \
  "$imgui/imgui_draw.cpp" \
  "$imgui/imgui_tables.cpp" \
  "$imgui/imgui_widgets.cpp" \
  "$imgui/backends/imgui_impl_glfw.cpp" \
  "$imgui/backends/imgui_impl_opengl3.cpp" \
  -sUSE_GLFW=3 \
  -sMIN_WEBGL_VERSION=2 \
  -sMAX_WEBGL_VERSION=2 \
  -sFULL_ES3=1 \
  -sALLOW_MEMORY_GROWTH=1 \
  -sENVIRONMENT=web \
  -sFILESYSTEM=0 \
  -sMODULARIZE=1 \
  -sEXPORT_NAME=createTessDiagnostics \
  -sEXPORTED_FUNCTIONS="$diagnostics_exports" \
  -sEXPORTED_RUNTIME_METHODS='["cwrap"]' \
  -o "$diagnostics/tess-diagnostics.js"

# WebGPU compute smoke: Emdawnwebgpu's stable C API, a real WGSL dispatch,
# and explicit summary readback. Adapter absence is a supported runtime state.
webgpu="$output/webgpu"
mkdir -p "$webgpu"
cp "$root/examples/webgpu_compute/site/index.html" "$webgpu/"
cp "$root/examples/webgpu_compute/site/app.js" "$webgpu/"
cp "$root/examples/web_pathfinder/site/style.css" "$webgpu/"
cp "$root/examples/web_pathfinder/site/favicon.svg" "$webgpu/"
cp "$root/docs/assets/tess-logo-dark.svg" "$webgpu/logo.svg"

em++ \
  -std=c++20 \
  -O3 \
  -DNDEBUG \
  -I"$root/include" \
  -I"$config/generated/include" \
  --use-port=emdawnwebgpu \
  --closure=1 \
  "$root/examples/webgpu_compute/webgpu_compute.cc" \
  -sALLOW_MEMORY_GROWTH=1 \
  -sENVIRONMENT=web \
  -sFILESYSTEM=0 \
  -sMODULARIZE=1 \
  -sEXPORT_NAME=createTessWebGpu \
  -sEXPORTED_FUNCTIONS='["_main","_tess_webgpu_status"]' \
  -sEXPORTED_RUNTIME_METHODS='["cwrap"]' \
  -o "$webgpu/tess-webgpu.js"
