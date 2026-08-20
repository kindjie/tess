#include <benchmark/benchmark.h>
#include <tess/tess.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// `lab/` family: exploratory measurements with no threshold targets and no
// baseline collection (the threshold jobs filter by family prefix). These
// answer the screening study's open cost question — what joint admission
// costs at colony scale — without joining the calibrated gate set.

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

void fill_world(World& world) {
  for (auto& page : world.chunks()) {
    auto passable = page.template field_span<PassableTag>();
    auto cost = page.template field_span<CostTag>();
    for (std::size_t i = 0; i < passable.size(); ++i) {
      passable[i] = true;
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

// Steady state: head-on pairs under Forbid re-run the whole admission
// pipeline every call (validation, occupant index, chain fixpoint, cycle
// walk, policy denial) without changing world or agent state — occupancy
// failures retain the Found route, so every agent stays eligible.
void BM_joint_headon_denied(benchmark::State& state) {
  const auto pair_count = static_cast<std::size_t>(state.range(0));
  World world;
  fill_world(world);
  std::vector<tess::PathAgentState> agents;
  tess::PathAgentRoutes routes;
  for (std::size_t pair = 0; pair < pair_count; ++pair) {
    const auto x = static_cast<std::int64_t>(2 * (pair % 60) + 2);
    const auto y = static_cast<std::int64_t>(pair / 60 + 1);
    add_agent(world, agents, routes, {{x, y, 0}, {x + 1, y, 0}});
    add_agent(world, agents, routes, {{x + 1, y, 0}, {x, y, 0}});
  }
  tess::JointMoveScratch scratch;
  scratch.reserve(agents.size());
  tess::JointMoveStats stats;
  for (auto _ : state) {
    stats = tess::advance_path_agents_with_joint_movement<
        World, PassableTag, OccupancyTag, ReservationTag>(
        world, std::span<tess::PathAgentState>(agents), routes, scratch);
    benchmark::DoNotOptimize(stats.swaps_denied);
    benchmark::ClobberMemory();
  }
  if (stats.swaps_denied != pair_count) {
    state.SkipWithError("expected every pair to be denied");
  }
  state.counters["agents"] = static_cast<double>(agents.size());
}

// Chains admitted end to end. Movement changes state, so each iteration
// rebuilds agent positions and occupancy before advancing; the reset is O(n)
// writes and is included in the measured time, which this lab entry accepts
// in exchange for exercising the apply path.
void BM_joint_chain_reset(benchmark::State& state) {
  const auto agent_count = static_cast<std::size_t>(state.range(0));
  const auto per_row = std::size_t{100};
  World world;
  fill_world(world);
  std::vector<tess::PathAgentState> agents;
  tess::PathAgentRoutes routes;
  for (std::size_t i = 0; i < agent_count; ++i) {
    const auto x = static_cast<std::int64_t>(i % per_row + 1);
    const auto y = static_cast<std::int64_t>(i / per_row + 1);
    add_agent(world, agents, routes, {{x, y, 0}, {x + 1, y, 0}, {x + 2, y, 0}});
  }
  tess::JointMoveScratch scratch;
  scratch.reserve(agents.size());
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
    stats = tess::advance_path_agents_with_joint_movement<
        World, PassableTag, OccupancyTag, ReservationTag>(
        world, std::span<tess::PathAgentState>(agents), routes, scratch);
    benchmark::DoNotOptimize(stats.frame.advanced);
    benchmark::ClobberMemory();
  }
  if (stats.frame.advanced != agent_count) {
    state.SkipWithError("expected every agent to advance");
  }
  state.counters["agents"] = static_cast<double>(agent_count);
}

BENCHMARK(BM_joint_headon_denied)
    ->Name("lab/joint_headon_denied_128x128")
    ->Arg(64)
    ->Arg(256)
    ->Arg(512);
BENCHMARK(BM_joint_chain_reset)
    ->Name("lab/joint_chain_reset_128x128")
    ->Arg(128)
    ->Arg(512)
    ->Arg(1024);

}  // namespace
