#include <tess/core/config.h>
#include <tess/tess.h>

#include <array>
#include <cstdint>
#include <exception>
#include <iostream>

namespace {

struct PassableTag {};
struct CostTag {};

using Shape = tess::Shape<tess::Extent3{32, 32}, tess::Extent3{8, 8}>;
using Schema = tess::FieldSchema<tess::Field<PassableTag, bool>,
                                 tess::Field<CostTag, std::uint32_t>>;
using World = tess::AlwaysResidentWorld<Shape, Schema>;

void mark_passable(World& world, tess::Coord3 coord, bool passable) {
  world.template field<PassableTag>(coord) = passable;
  world.mark_dirty(tess::chunk_key<Shape>(tess::tile_key<Shape>(coord)),
                   tess::DirtyMask{1u},
                   tess::Box3{coord, tess::Extent3{1, 1, 1}});
}

auto run() -> int {
  World world;
  world.fill_field<PassableTag>(true);
  world.fill_field<CostTag>(1u);

  std::array<tess::PathAgentState, 4> agents{{
      {.position = tess::Coord2{0, 0}},
      {.position = tess::Coord2{0, 1}},
      {.position = tess::Coord2{0, 2}},
      {.position = tess::Coord2{0, 3}},
  }};
  tess::PathAgentTickState tick_state;
  for (auto& agent : agents) {
    tess::set_path_agent_goal(tick_state, agent, tess::Coord2{31, 31});
  }

  tess::PathRequestRuntime runtime;
  runtime.reserve_requests(agents.size());
  runtime.reserve_search_nodes(Shape::size.x * Shape::size.y * Shape::size.z);
  runtime.reserve_path_nodes(4096);
  runtime.reserve_unit_routes(agents.size());

  const auto options = tess::PathAgentTickOptions{.max_steps = 4};
  auto tick = tess::tick_unit_path_agents<World, PassableTag>(
      tick_state, world, agents, runtime, options);
  auto stats = tick.pathing;
  if (stats.found != agents.size()) {
    std::cerr << "initial pathing failed\n";
    return 1;
  }

  (void)tess::tick_unit_path_agents<World, PassableTag>(
      tick_state, world, agents, runtime, options);

  mark_passable(world, tess::Coord2{12, 0}, false);
  tess::mark_pathing_dirty(tick_state);
  tick = tess::tick_unit_path_agents<World, PassableTag>(
      tick_state, world, agents, runtime, options);
  stats = tick.pathing;
  if (stats.found != agents.size()) {
    std::cerr << "re-search failed\n";
    return 1;
  }

  std::cout << "agents: " << agents.size() << "\n";
  std::cout << "tick: " << tick.tick << "\n";
  std::cout << "first agent: " << agents[0].position.x << ","
            << agents[0].position.y << "," << agents[0].position.z << "\n";
  std::cout << "runtime path nodes: " << runtime.stats().path_nodes << "\n";
  return 0;
}

}  // namespace

int main() {
#if TESS_HAS_EXCEPTIONS
  try {
#endif
    return run();
#if TESS_HAS_EXCEPTIONS
  } catch (const std::exception& err) {
    std::cerr << "example failed: " << err.what() << "\n";
    return 1;
  }
#endif
}
