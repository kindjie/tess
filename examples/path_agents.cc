#include <tess/tess.h>

#include <array>
#include <cstdint>
#include <iostream>

namespace {

struct PassableTag {};
struct CostTag {};

using Shape = tess::Shape<tess::Extent3{32, 32, 1}, tess::Extent3{8, 8, 1}>;
using Schema = tess::FieldSchema<tess::Field<PassableTag, bool>,
                                 tess::Field<CostTag, std::uint32_t>>;
using World = tess::AlwaysResidentWorld<Shape, Schema>;

template <typename FieldTag, typename Value>
void fill_field(World& world, Value value) {
  for (auto& page : world.chunks()) {
    auto field = page.template field_span<FieldTag>();
    for (auto& tile : field) {
      tile = value;
    }
  }
}

void mark_passable(World& world, tess::Coord3 coord, bool passable) {
  world.template field<PassableTag>(coord) = passable;
  world.mark_dirty(tess::chunk_key<Shape>(tess::tile_key<Shape>(coord)), 1u,
                   tess::Box3{coord, tess::Extent3{1, 1, 1}});
}

}  // namespace

int main() {
  World world;
  fill_field<PassableTag>(world, true);
  fill_field<CostTag>(world, 1u);

  std::array<tess::PathAgentState, 4> agents{{
      {.position = tess::Coord3{0, 0, 0}},
      {.position = tess::Coord3{0, 1, 0}},
      {.position = tess::Coord3{0, 2, 0}},
      {.position = tess::Coord3{0, 3, 0}},
  }};
  for (auto& agent : agents) {
    tess::set_path_agent_goal(agent, tess::Coord3{31, 31, 0});
  }

  tess::PathRequestRuntime runtime;
  runtime.reserve_requests(agents.size());
  runtime.reserve_search_nodes(Shape::size.x * Shape::size.y * Shape::size.z);
  runtime.reserve_path_nodes(4096);
  runtime.reserve_unit_routes(agents.size());

  auto stats = tess::process_unit_path_agents<World, PassableTag>(world, agents,
                                                                  runtime);
  if (stats.found != agents.size()) {
    std::cerr << "initial pathing failed\n";
    return 1;
  }

  (void)tess::advance_path_agents(agents, runtime, 4);

  mark_passable(world, tess::Coord3{8, 0, 0}, false);
  stats = tess::process_unit_path_agents<World, PassableTag>(world, agents,
                                                             runtime);
  if (stats.found != agents.size()) {
    std::cerr << "repath failed\n";
    return 1;
  }

  std::cout << "agents: " << agents.size() << "\n";
  std::cout << "first agent: " << agents[0].position.x << ","
            << agents[0].position.y << "," << agents[0].position.z << "\n";
  std::cout << "runtime path nodes: " << runtime.stats().path_nodes << "\n";
  return 0;
}
