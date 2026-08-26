add_executable(tess_sparse_stream sparse_stream.cc)
target_link_libraries(tess_sparse_stream PRIVATE tess::tess)
tess_apply_project_options(tess_sparse_stream)

add_executable(tess_quickstart quickstart.cc)
target_link_libraries(tess_quickstart PRIVATE tess::tess)
tess_apply_project_options(tess_quickstart)

add_executable(tess_chunk_maintenance chunk_maintenance.cc)
target_link_libraries(tess_chunk_maintenance PRIVATE tess::tess)
tess_apply_project_options(tess_chunk_maintenance)

add_executable(tess_congestion_pricing congestion_pricing.cc)
target_link_libraries(tess_congestion_pricing PRIVATE tess::tess)
tess_apply_project_options(tess_congestion_pricing)
if(BUILD_TESTING)
  add_test(NAME tess_congestion_pricing_example
           COMMAND tess_congestion_pricing)
  set_tests_properties(
    tess_congestion_pricing_example
    PROPERTIES
      LABELS "target:tess_congestion_pricing;subsystem:path;subsystem:sim"
      PASS_REGULAR_EXPRESSION "congestion pricing example: OK"
  )
endif()

add_executable(tess_documentation documentation.cc)
target_link_libraries(tess_documentation PRIVATE tess::tess)
tess_apply_project_options(tess_documentation)

add_executable(
  tess_web_congestion_model
  web_congestion/congestion_model.cc
  web_congestion/congestion_native.cc
  web_colony/colony_model.cc
)
target_link_libraries(tess_web_congestion_model PRIVATE tess::tess)
tess_apply_project_options(tess_web_congestion_model)

# Compile the browser adapter under native warnings and diff clang-tidy
# without coupling it to the native verification executable.
add_library(
  tess_web_congestion_wasm_adapter OBJECT
  web_congestion/congestion_wasm.cc
  web_congestion/congestion_model.cc
  web_colony/colony_model.cc
)
target_link_libraries(tess_web_congestion_wasm_adapter PRIVATE tess::tess)
tess_apply_project_options(tess_web_congestion_wasm_adapter)
if(BUILD_TESTING)
  add_test(
    NAME tess_web_congestion_scenario_smoke
    COMMAND tess_web_congestion_model --scenario two-gates --agents 128
            --policy 9 --max-ticks 1000
  )
  set_tests_properties(
    tess_web_congestion_scenario_smoke
    PROPERTIES
      LABELS "target:tess_web_congestion_model;subsystem:path;subsystem:sim"
      PASS_REGULAR_EXPRESSION
        "scenario=two-gates.*arrived=128 crowd_blocked=0 unreachable=0 turnaround=1 walls=ok"
  )
endif()

add_executable(
  tess_web_colony_model
  web_colony/colony_model.cc
  web_colony/colony_native.cc
)
target_link_libraries(tess_web_colony_model PRIVATE tess::tess)
tess_apply_project_options(tess_web_colony_model)
if(BUILD_TESTING)
  add_test(
    NAME tess_web_colony_scenario_smoke
    COMMAND tess_web_colony_model --scenario two-gates --agents 128
            --mode spread --max-ticks 1000 --require-complete
  )
  set_tests_properties(
    tess_web_colony_scenario_smoke
    PROPERTIES
      LABELS "target:tess_web_colony_model;subsystem:path;subsystem:sim"
      PASS_REGULAR_EXPRESSION
        "scenario=two-gates.*arrived=128.*crowd=0.*unreachable=0"
  )
  add_test(
    NAME tess_web_colony_multigate_spread_guard
    COMMAND tess_web_colony_model --scenario four-gates --agents 512
            --mode spread --max-ticks 1000 --require-complete
  )
  set_tests_properties(
    tess_web_colony_multigate_spread_guard
    PROPERTIES
      LABELS "target:tess_web_colony_model;subsystem:path;subsystem:sim"
      PASS_REGULAR_EXPRESSION
        "scenario=four-gates.*arrived=512.*crowd=0.*diversity_waves=0"
  )
  add_test(
    NAME tess_web_colony_incremental_wall_spread
    COMMAND tess_web_colony_model --scenario browser-incremental --agents 1024
            --mode spread --max-ticks 1000 --require-complete
  )
  set_tests_properties(
    tess_web_colony_incremental_wall_spread
    PROPERTIES
      LABELS "target:tess_web_colony_model;subsystem:path;subsystem:sim"
      PASS_REGULAR_EXPRESSION
        "browser-incremental.*complete=1.*walls_accepted=297.*diversity_waves=1"
  )
  add_test(
    NAME tess_web_colony_endpoint_crosscut_guard
    COMMAND tess_web_colony_model --scenario browser-guard --agents 896
            --mode spread --max-ticks 1000 --require-complete
  )
  set_tests_properties(
    tess_web_colony_endpoint_crosscut_guard
    PROPERTIES
      LABELS "target:tess_web_colony_model;subsystem:path;subsystem:sim"
      PASS_REGULAR_EXPRESSION
        "scenario=browser-guard.*arrived=896.*crowd=0.*unreachable=0"
  )
  add_test(
    NAME tess_web_colony_canonical_endpoint_crosscut_guard
    COMMAND tess_web_colony_model --scenario browser-guard --agents 896
            --mode canonical --max-ticks 5000 --require-complete
  )
  set_tests_properties(
    tess_web_colony_canonical_endpoint_crosscut_guard
    PROPERTIES
      LABELS "target:tess_web_colony_model;subsystem:path;subsystem:sim"
      TIMEOUT 240
      PASS_REGULAR_EXPRESSION
        "scenario=browser-guard.*arrived=896.*crowd=0.*unreachable=0"
  )
endif()

# Compile the browser adapter under native warnings and diff clang-tidy
# without coupling it to the native verification executable.
add_library(tess_web_colony_wasm_adapter OBJECT web_colony/colony_wasm.cc)
target_link_libraries(tess_web_colony_wasm_adapter PRIVATE tess::tess)
tess_apply_project_options(tess_web_colony_wasm_adapter)

add_executable(
  tess_web_traffic_model
  web_traffic/traffic_model.cc
  web_traffic/traffic_native.cc
)
target_link_libraries(tess_web_traffic_model PRIVATE tess::tess)
tess_apply_project_options(tess_web_traffic_model)
if(BUILD_TESTING)
  set(traffic_model_labels
    "target:tess_web_traffic_model;subsystem:path;subsystem:sim"
  )
  add_test(
    NAME tess_web_traffic_catalog
    COMMAND tess_web_traffic_model --self-check-catalog
  )
  set_tests_properties(tess_web_traffic_catalog PROPERTIES
    LABELS "${traffic_model_labels}"
    TIMEOUT 60
  )
  add_test(
    NAME tess_web_traffic_aligned
    COMMAND tess_web_traffic_model --self-check aligned
  )
  set_tests_properties(tess_web_traffic_aligned PROPERTIES
    LABELS "${traffic_model_labels}"
    TIMEOUT 240
  )
  add_test(
    NAME tess_web_traffic_shuffled_crossing
    COMMAND tess_web_traffic_model --self-check shuffled-crossing
  )
  set_tests_properties(tess_web_traffic_shuffled_crossing PROPERTIES
    LABELS "${traffic_model_labels}"
    TIMEOUT 240
  )
  add_test(
    NAME tess_web_traffic_funnel_smoke
    COMMAND tess_web_traffic_model --self-check-smoke funnel
  )
  set_tests_properties(tess_web_traffic_funnel_smoke PROPERTIES
    LABELS "${traffic_model_labels}"
    TIMEOUT 60
  )
  add_test(
    NAME tess_web_traffic_multi_gate_smoke
    COMMAND tess_web_traffic_model --self-check-smoke multi-gate
  )
  set_tests_properties(tess_web_traffic_multi_gate_smoke PROPERTIES
    LABELS "${traffic_model_labels}"
    TIMEOUT 60
  )
  add_test(
    NAME tess_web_traffic_funnel_crowd
    COMMAND tess_web_traffic_model --self-check-crowd funnel
  )
  set_tests_properties(tess_web_traffic_funnel_crowd PROPERTIES
    LABELS "${traffic_model_labels};config:tsan-exempt"
    TIMEOUT 600
  )
  add_test(
    NAME tess_web_traffic_multi_gate_crowd
    COMMAND tess_web_traffic_model --self-check-crowd multi-gate
  )
  set_tests_properties(tess_web_traffic_multi_gate_crowd PROPERTIES
    LABELS "${traffic_model_labels};config:tsan-exempt"
    TIMEOUT 300
  )
  add_test(
    NAME tess_web_traffic_funnel
    COMMAND tess_web_traffic_model --self-check-exhaustive funnel
  )
  set_tests_properties(tess_web_traffic_funnel PROPERTIES
    LABELS
      "${traffic_model_labels};config:tsan-exempt;config:optimized-exhaustive"
    TIMEOUT 600
  )
  add_test(
    NAME tess_web_traffic_multi_gate
    COMMAND tess_web_traffic_model --self-check-exhaustive multi-gate
  )
  set_tests_properties(tess_web_traffic_multi_gate PROPERTIES
    LABELS
      "${traffic_model_labels};config:tsan-exempt;config:optimized-exhaustive"
    TIMEOUT 300
  )
  add_test(
    NAME tess_web_traffic_crowd_rejects_unguided
    COMMAND tess_web_traffic_model --self-check-crowd aligned
  )
  set_tests_properties(tess_web_traffic_crowd_rejects_unguided PROPERTIES
    LABELS "${traffic_model_labels}"
    TIMEOUT 60
    WILL_FAIL TRUE
  )
  add_test(
    NAME tess_web_traffic_exhaustive_rejects_unguided
    COMMAND tess_web_traffic_model --self-check-exhaustive shuffled-crossing
  )
  set_tests_properties(tess_web_traffic_exhaustive_rejects_unguided PROPERTIES
    LABELS "${traffic_model_labels}"
    TIMEOUT 60
    WILL_FAIL TRUE
  )
  add_test(
    NAME tess_web_traffic_measurement
    COMMAND tess_web_traffic_model
            --scenario aligned --ticks 20 --samples
  )
  set_tests_properties(
    tess_web_traffic_measurement
    PROPERTIES
      LABELS "${traffic_model_labels}"
      PASS_REGULAR_EXPRESSION "aligned,19,"
  )
endif()

add_library(tess_web_traffic_wasm_adapter OBJECT
            web_traffic/traffic_wasm.cc)
target_link_libraries(tess_web_traffic_wasm_adapter PRIVATE tess::tess)
tess_apply_project_options(tess_web_traffic_wasm_adapter)

add_executable(
  tess_web_traffic_diagnostics_model
  web_traffic/traffic_model.cc
  web_traffic/traffic_native.cc
)
target_link_libraries(tess_web_traffic_diagnostics_model PRIVATE tess::tess)
target_compile_definitions(
  tess_web_traffic_diagnostics_model PRIVATE TESS_ENABLE_DIAGNOSTICS
)
tess_apply_project_options(tess_web_traffic_diagnostics_model)
if(BUILD_TESTING)
  add_test(
    NAME tess_web_traffic_diagnostics
    COMMAND tess_web_traffic_diagnostics_model
            --scenario aligned --ticks 1 --samples
  )
  set_tests_properties(
    tess_web_traffic_diagnostics
    PROPERTIES
      LABELS "target:tess_web_traffic_diagnostics_model;subsystem:path"
      PASS_REGULAR_EXPRESSION ",0,0,0,[1-9][0-9]*,[1-9][0-9]*"
  )
endif()

add_executable(tess_web_pathfinder_model web_pathfinder/pathfinder.cc)
target_link_libraries(tess_web_pathfinder_model PRIVATE tess::tess)
tess_apply_project_options(tess_web_pathfinder_model)

add_executable(
  tess_web_diagnostics_model
  web_diagnostics/diagnostics_model.cc
  web_diagnostics/diagnostics_native.cc
)
target_link_libraries(tess_web_diagnostics_model PRIVATE tess::tess)
target_compile_definitions(
  tess_web_diagnostics_model PRIVATE TESS_ENABLE_DIAGNOSTICS
)
tess_apply_project_options(tess_web_diagnostics_model)

add_executable(tess_queued_path queued_path.cc)
target_link_libraries(tess_queued_path PRIVATE tess::tess)
tess_apply_project_options(tess_queued_path)

add_executable(tess_path_agents path_agents.cc)
target_link_libraries(tess_path_agents PRIVATE tess::tess)
tess_apply_project_options(tess_path_agents)

add_executable(
  tess_pathfinding_strategies
  pathfinding_strategies_model.cc
  pathfinding_strategies.cc
)
target_link_libraries(tess_pathfinding_strategies PRIVATE tess::tess)
tess_apply_project_options(tess_pathfinding_strategies)

# Compile the browser adapter under native warnings and diff clang-tidy while
# keeping the pathfinding calls in the shared model above.
add_library(
  tess_pathfinding_strategies_wasm_adapter OBJECT
  web_pathfinding_strategies/strategies_wasm.cc
)
target_link_libraries(
  tess_pathfinding_strategies_wasm_adapter PRIVATE tess::tess
)
tess_apply_project_options(tess_pathfinding_strategies_wasm_adapter)

add_executable(tess_custom_ecs_min custom_ecs_min.cc)
target_link_libraries(tess_custom_ecs_min PRIVATE tess::tess)
tess_apply_project_options(tess_custom_ecs_min)

add_executable(tess_render_delta_consumer render_delta_consumer.cc)
target_link_libraries(tess_render_delta_consumer PRIVATE tess::tess)
tess_apply_project_options(tess_render_delta_consumer)

add_executable(tess_colony_2d colony_2d.cc)
target_link_libraries(tess_colony_2d PRIVATE tess::tess)
tess_apply_project_options(tess_colony_2d)

add_executable(tess_ant_farm_vertical ant_farm_vertical.cc)
target_link_libraries(tess_ant_farm_vertical PRIVATE tess::tess)
tess_apply_project_options(tess_ant_farm_vertical)

add_executable(tess_stairs_3d stairs_3d.cc)
target_link_libraries(tess_stairs_3d PRIVATE tess::tess)
tess_apply_project_options(tess_stairs_3d)

if(TESS_ENABLE_ENTT)
  include(TessEnttDeps)
  tess_require_entt()

  add_executable(tess_entt_pawns entt_pawns.cc)
  target_link_libraries(tess_entt_pawns PRIVATE tess::tess EnTT::EnTT)
  # Per-target header gate by design; see tests/CMakeLists.txt.
  target_compile_definitions(tess_entt_pawns PRIVATE TESS_ENABLE_ENTT)
  tess_apply_project_options(tess_entt_pawns)
endif()

if(TESS_ENABLE_FLECS)
  include(TessFlecsDeps)
  tess_require_flecs()

  add_executable(tess_flecs_pawns flecs_pawns.cc)
  target_link_libraries(tess_flecs_pawns PRIVATE tess::tess
                                                  flecs::flecs_static)
  target_compile_definitions(tess_flecs_pawns PRIVATE TESS_ENABLE_FLECS)
  tess_apply_project_options(tess_flecs_pawns)
endif()

if(TESS_EXAMPLES_NO_EXCEPTIONS)
  get_property(example_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
  foreach(target IN LISTS example_targets)
    tess_target_disable_exceptions(${target})
  endforeach()
endif()
