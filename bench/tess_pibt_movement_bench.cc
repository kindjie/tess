#include <benchmark/benchmark.h>
#include <tess/tess.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <vector>

// `lab/` family: exploratory measurements with no threshold targets and no
// baseline collection (the threshold jobs filter by family prefix). These
// mirror the joint-commit lab entries one for one so the PIBT tier's cost
// multiple is a direct read: same world, same agent layouts, same reset
// accounting — only the decision procedure differs.

namespace {

struct PassableTag {};
struct CostTag {};
struct OccupancyTag {};
struct ReservationTag {};

using Colony2D =
    tess::Shape<tess::Extent3{128, 128, 1}, tess::Extent3{16, 16, 1}>;
using Schema = tess::FieldSchema<
    tess::Field<PassableTag, bool>, tess::Field<CostTag, std::uint32_t>,
    tess::Field<OccupancyTag, bool>, tess::Field<ReservationTag, bool>>;
using World = tess::AlwaysResidentWorld<Colony2D, Schema>;

void fill_world(World& world, bool passable) {
  for (auto& page : world.chunks()) {
    auto open = page.template field_span<PassableTag>();
    auto cost = page.template field_span<CostTag>();
    for (std::size_t i = 0; i < open.size(); ++i) {
      open[i] = passable;
      cost[i] = 1u;
    }
  }
}

void add_agent(World& world, std::vector<tess::PathAgentState>& agents,
               tess::PathAgentRoutes& routes, std::vector<tess::Coord3> route) {
  tess::PathAgentState agent;
  agent.position = route.front();
  agent.goal = route.back();
  agent.has_goal = true;
  agent.last_result = tess::PathStatus::Found;
  agent.phase = tess::PathAgentPhase::Following;
  world.field<OccupancyTag>(agent.position) = true;
  agents.push_back(agent);
  routes.routes.push_back(std::move(route));
}

// Steady state: head-on pairs boxed in two-tile pockets under Forbid. Unlike
// the joint entry's open plain, PIBT on open ground resolves head-ons by
// yielding sideways, so the steady state needs walls; each pair re-runs the
// full decision pipeline every call (priority order, candidate ranking,
// inheritance into the boxed peer, backtracking, edge-conflict denial)
// without changing world or agent state.
void BM_pibt_headon_denied(benchmark::State& state) {
  const auto pair_count = static_cast<std::size_t>(state.range(0));
  World world;
  fill_world(world, false);
  std::vector<tess::PathAgentState> agents;
  tess::PathAgentRoutes routes;
  for (std::size_t pair = 0; pair < pair_count; ++pair) {
    const auto x = static_cast<std::int64_t>(3 * (pair % 40) + 1);
    const auto y = static_cast<std::int64_t>(2 * (pair / 40) + 1);
    world.field<PassableTag>(tess::Coord3{x, y, 0}) = true;
    world.field<PassableTag>(tess::Coord3{x + 1, y, 0}) = true;
    add_agent(world, agents, routes, {{x, y, 0}, {x + 1, y, 0}});
    add_agent(world, agents, routes, {{x + 1, y, 0}, {x, y, 0}});
  }
  tess::PibtPriorities priorities;
  priorities.reserve(agents.size());
  tess::JointMoveScratch scratch;
  scratch.reserve(agents.size());
  const auto rank = [&agents](std::size_t agent,
                              tess::Coord3 c) -> std::uint32_t {
    const auto goal = agents[agent].goal;
    return static_cast<std::uint32_t>(std::abs(c.x - goal.x) +
                                      std::abs(c.y - goal.y));
  };
  tess::JointMoveStats stats;
  for (auto _ : state) {
    stats = tess::advance_path_agents_with_pibt<World, PassableTag,
                                                OccupancyTag, ReservationTag>(
        world, std::span<tess::PathAgentState>(agents), routes, priorities,
        scratch, rank);
    benchmark::DoNotOptimize(stats.swaps_denied);
    benchmark::ClobberMemory();
  }
  if (stats.frame.advanced != 0) {
    state.SkipWithError("expected every pair to hold still");
  }
  state.counters["agents"] = static_cast<double>(agents.size());
}

// Chains admitted end to end, identical layout and reset accounting to the
// joint entry: adjacent agents marching east resolve through one long
// inheritance recursion per row instead of the joint pass's vacated-chain
// fixpoint.
void BM_pibt_chain_reset(benchmark::State& state) {
  const auto agent_count = static_cast<std::size_t>(state.range(0));
  const auto per_row = std::size_t{100};
  World world;
  fill_world(world, true);
  std::vector<tess::PathAgentState> agents;
  tess::PathAgentRoutes routes;
  for (std::size_t i = 0; i < agent_count; ++i) {
    const auto x = static_cast<std::int64_t>(i % per_row + 1);
    const auto y = static_cast<std::int64_t>(i / per_row + 1);
    add_agent(world, agents, routes, {{x, y, 0}, {x + 1, y, 0}, {x + 2, y, 0}});
  }
  tess::PibtPriorities priorities;
  priorities.reserve(agents.size());
  tess::JointMoveScratch scratch;
  scratch.reserve(agents.size());
  const auto rank = [&agents](std::size_t agent,
                              tess::Coord3 c) -> std::uint32_t {
    const auto goal = agents[agent].goal;
    return static_cast<std::uint32_t>(std::abs(c.x - goal.x) +
                                      std::abs(c.y - goal.y));
  };
  tess::JointMoveStats stats;
  for (auto _ : state) {
    for (std::size_t i = 0; i < agents.size(); ++i) {
      auto& agent = agents[i];
      world.field<OccupancyTag>(agent.position) = false;
      agent.position = routes.routes[i].front();
      agent.path_index = 0;
      agent.last_result = tess::PathStatus::Found;
      agent.phase = tess::PathAgentPhase::Following;
      agent.has_goal = true;
      world.field<OccupancyTag>(agent.position) = true;
    }
    stats = tess::advance_path_agents_with_pibt<World, PassableTag,
                                                OccupancyTag, ReservationTag>(
        world, std::span<tess::PathAgentState>(agents), routes, priorities,
        scratch, rank);
    benchmark::DoNotOptimize(stats.frame.advanced);
    benchmark::ClobberMemory();
  }
  if (stats.frame.advanced != agent_count) {
    state.SkipWithError("expected every agent to advance");
  }
  state.counters["agents"] = static_cast<double>(agent_count);
}

BENCHMARK(BM_pibt_headon_denied)
    ->Name("lab/pibt_headon_denied_128x128")
    ->Arg(64)
    ->Arg(256)
    ->Arg(512);
BENCHMARK(BM_pibt_chain_reset)
    ->Name("lab/pibt_chain_reset_128x128")
    ->Arg(128)
    ->Arg(512)
    ->Arg(1024);

}  // namespace
