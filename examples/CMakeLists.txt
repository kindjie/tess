add_executable(tess_sparse_stream sparse_stream.cc)
target_link_libraries(tess_sparse_stream PRIVATE tess::tess)
tess_apply_project_options(tess_sparse_stream)

add_executable(tess_quickstart quickstart.cc)
target_link_libraries(tess_quickstart PRIVATE tess::tess)
tess_apply_project_options(tess_quickstart)

add_executable(tess_documentation documentation.cc)
target_link_libraries(tess_documentation PRIVATE tess::tess)
tess_apply_project_options(tess_documentation)

add_executable(tess_web_colony_model web_colony/colony.cc)
target_link_libraries(tess_web_colony_model PRIVATE tess::tess)
tess_apply_project_options(tess_web_colony_model)

add_executable(tess_web_pathfinder_model web_pathfinder/pathfinder.cc)
target_link_libraries(tess_web_pathfinder_model PRIVATE tess::tess)
tess_apply_project_options(tess_web_pathfinder_model)

add_executable(tess_mvp_path mvp_path.cc)
target_link_libraries(tess_mvp_path PRIVATE tess::tess)
tess_apply_project_options(tess_mvp_path)

add_executable(tess_path_agents path_agents.cc)
target_link_libraries(tess_path_agents PRIVATE tess::tess)
tess_apply_project_options(tess_path_agents)

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
