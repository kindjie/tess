add_executable(tess_mvp_path mvp_path.cc)
target_link_libraries(tess_mvp_path PRIVATE tess::tess)
tess_apply_project_options(tess_mvp_path)

add_executable(tess_path_agents path_agents.cc)
target_link_libraries(tess_path_agents PRIVATE tess::tess)
tess_apply_project_options(tess_path_agents)
