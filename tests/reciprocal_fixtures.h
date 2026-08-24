// Shared reciprocal-conflict fixtures, oracle, and bound (extracted
// verbatim from tess_reciprocal_conflict_test.cc so PR C4's escalation
// gates can drive the same instances; the C3 digest table still pins
// them). Test support only, never a public header.
#pragma once

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "movement_scenarios.h"

namespace tess_test::movement::reciprocal {

namespace mv = tess_test::movement;

struct AgentSpec {
  tess::Coord3 start{};
  tess::Coord3 goal{};
};

// '.' is open terrain; anything else is blocked. Row y is rows[y], so
// the maps below read exactly as drawn.
struct Fixture {
  const char* name = "";
  tess::SwapPolicy swap = tess::SwapPolicy::Forbid;
  std::vector<std::string> rows;
  std::vector<AgentSpec> agents;
};

// F1 pocket-yield corridor (Forbid): one head-on pair, one side pocket.
// Resolvable only by a yield into the pocket.
inline const Fixture kPocketYield{
    "pocket_yield/forbid",
    tess::SwapPolicy::Forbid,
    {
        ".......",
        "   .   ",
    },
    {{{0, 0, 0}, {6, 0, 0}}, {{6, 0, 0}, {0, 0, 0}}},
};

// F2 head-on corridor (Permit): no pocket, resolvable only by a swap.
inline const Fixture kHeadOnSwap{
    "head_on/permit",
    tess::SwapPolicy::Permit,
    {
        ".....",
    },
    {{{0, 0, 0}, {4, 0, 0}}, {{4, 0, 0}, {0, 0, 0}}},
};

// F3 rotation cycle: four agents on a 2x2 ring, each goal one step
// around. Resolvable only by a simultaneous cycle rotation, which is
// not a pairwise exchange, so it is legal under both policies.
inline const std::vector<std::string> kRingRows = {
    "..",
    "..",
};
inline const std::vector<AgentSpec> kRingAgents = {
    {{0, 0, 0}, {1, 0, 0}},
    {{1, 0, 0}, {1, 1, 0}},
    {{1, 1, 0}, {0, 1, 0}},
    {{0, 1, 0}, {0, 0, 0}},
};
inline const Fixture kRotationForbid{
    "rotation/forbid", tess::SwapPolicy::Forbid, kRingRows, kRingAgents};
inline const Fixture kRotationPermit{
    "rotation/permit", tess::SwapPolicy::Permit, kRingRows, kRingAgents};

// F4 junction cross (Forbid): two head-on pairs crossing a plus
// junction; the arms double as the only yield space.
inline const Fixture kJunctionCross{
    "junction_cross/forbid",
    tess::SwapPolicy::Forbid,
    {
        "  .  ",
        "  .  ",
        ".....",
        "  .  ",
        "  .  ",
    },
    {{{0, 2, 0}, {4, 2, 0}},
     {{4, 2, 0}, {0, 2, 0}},
     {{2, 0, 0}, {2, 4, 0}},
     {{2, 4, 0}, {2, 0, 0}}},
};

// F5 queued yields (Forbid): two opposing pairs, two pockets, so one
// yield has to happen behind another.
inline const Fixture kQueuedYields{
    "queued_yields/forbid",
    tess::SwapPolicy::Forbid,
    {
        ".........",
        "   . .   ",
    },
    {{{0, 0, 0}, {8, 0, 0}},
     {{1, 0, 0}, {7, 0, 0}},
     {{8, 0, 0}, {0, 0, 0}},
     {{7, 0, 0}, {1, 0, 0}}},
};

// ---------------------------------------------------------------------
// Oracle: exhaustive joint-configuration BFS, independent of every
// library search. Synchronous MAPF model matching the tiers: one face
// step or wait per agent per tick; vertex conflicts always forbidden;
// pairwise exchanges forbidden under Forbid and permitted under Permit;
// moving into a tile vacated the same tick is legal (cycle rotations
// included).
// ---------------------------------------------------------------------

constexpr int kOracleUnsolved = -1;
constexpr std::size_t kOracleStateCap = 20'000'000;

[[nodiscard]] inline int oracle_optimal_makespan(const Fixture& fixture) {
  std::vector<tess::Coord3> cells;
  const int height = static_cast<int>(fixture.rows.size());
  int width = 0;
  for (const auto& row : fixture.rows) {
    width = std::max(width, static_cast<int>(row.size()));
  }
  std::vector<int> cell_at(
      static_cast<std::size_t>(width) * static_cast<std::size_t>(height), -1);
  for (int y = 0; y < height; ++y) {
    const auto& row = fixture.rows[static_cast<std::size_t>(y)];
    for (int x = 0; x < static_cast<int>(row.size()); ++x) {
      if (row[static_cast<std::size_t>(x)] == '.') {
        cell_at[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                static_cast<std::size_t>(x)] = static_cast<int>(cells.size());
        cells.push_back(tess::Coord3{x, y, 0});
      }
    }
  }
  const auto cell_of = [&](tess::Coord3 c) -> int {
    if (c.x < 0 || c.y < 0 || c.x >= width || c.y >= height) return -1;
    return cell_at[static_cast<std::size_t>(c.y) *
                       static_cast<std::size_t>(width) +
                   static_cast<std::size_t>(c.x)];
  };
  const auto n = fixture.agents.size();
  EXPECT_LE(n, 5u);
  std::vector<int> start(n), goal(n);
  for (std::size_t i = 0; i < n; ++i) {
    start[i] = cell_of(fixture.agents[i].start);
    goal[i] = cell_of(fixture.agents[i].goal);
    EXPECT_GE(start[i], 0);
    EXPECT_GE(goal[i], 0);
  }
  // Per-cell neighbor lists including "stay".
  std::vector<std::vector<int>> moves(cells.size());
  for (std::size_t c = 0; c < cells.size(); ++c) {
    moves[c].push_back(static_cast<int>(c));
    const auto base = cells[c];
    const std::array<tess::Coord3, 4> steps = {
        tess::Coord3{base.x + 1, base.y, 0},
        tess::Coord3{base.x - 1, base.y, 0},
        tess::Coord3{base.x, base.y + 1, 0},
        tess::Coord3{base.x, base.y - 1, 0}};
    for (const auto step : steps) {
      const auto id = cell_of(step);
      if (id >= 0) moves[c].push_back(id);
    }
  }
  const auto pack = [&](const std::vector<int>& state) {
    std::uint64_t key = 0;
    for (const auto cell : state) {
      key = key * cells.size() + static_cast<std::uint64_t>(cell);
    }
    return key;
  };
  const bool allow_swap = fixture.swap == tess::SwapPolicy::Permit;
  std::unordered_set<std::uint64_t> seen;
  std::deque<std::pair<std::vector<int>, int>> frontier;
  seen.insert(pack(start));
  frontier.emplace_back(start, 0);
  std::vector<int> next(n);
  while (!frontier.empty()) {
    // Plain members rather than structured bindings: the analyzer cannot
    // model bindings captured by the compose lambda and reports a
    // spurious undefined dereference.
    const auto entry = std::move(frontier.front());
    frontier.pop_front();
    const auto& state = entry.first;
    const auto depth = entry.second;
    if (state == goal) return depth;
    if (seen.size() > kOracleStateCap) return kOracleUnsolved;
    // Compose one agent at a time, pruning vertex conflicts and (under
    // Forbid) pairwise exchanges against already-placed agents.
    const auto compose = [&](auto&& self, std::size_t agent) -> void {
      if (agent == n) {
        const auto key = pack(next);
        if (seen.insert(key).second) {
          frontier.emplace_back(next, depth + 1);
        }
        return;
      }
      for (const auto target : moves[static_cast<std::size_t>(state[agent])]) {
        bool legal = true;
        for (std::size_t prior = 0; prior < agent; ++prior) {
          if (next[prior] == target) {
            legal = false;
            break;
          }
          if (!allow_swap && next[prior] == state[agent] &&
              target == state[prior]) {
            legal = false;
            break;
          }
        }
        if (!legal) continue;
        next[agent] = target;
        self(self, agent + 1);
      }
    };
    compose(compose, 0);
  }
  return kOracleUnsolved;
}

// ---------------------------------------------------------------------
// Tier runners over the C0 substrate.
// ---------------------------------------------------------------------

[[nodiscard]] inline auto build_fixture_scenario(const Fixture& fixture)
    -> std::unique_ptr<mv::Scenario> {
  auto scenario = std::make_unique<mv::Scenario>();
  scenario->options = mv::FixtureOptions{};
  scenario->options.swap = fixture.swap;
  const int height = static_cast<int>(fixture.rows.size());
  int width = 0;
  for (const auto& row : fixture.rows) {
    width = std::max(width, static_cast<int>(row.size()));
  }
  const int extent = std::max(width, height);
  scenario->options.extent = extent;
  scenario->terrain.assign(
      static_cast<std::size_t>(extent) * static_cast<std::size_t>(extent), 0u);
  mv::fill_world(scenario->world);
  for (int y = 0; y < height; ++y) {
    const auto& row = fixture.rows[static_cast<std::size_t>(y)];
    for (int x = 0; x < static_cast<int>(row.size()); ++x) {
      if (row[static_cast<std::size_t>(x)] != '.') continue;
      mv::grid_set(scenario->terrain, extent, x, y, true);
      scenario->world.field<mv::PassableTag>(tess::Coord3{x, y, 0}) = true;
    }
  }
  for (const auto& spec : fixture.agents) {
    tess::PathAgentState agent;
    agent.position = spec.start;
    scenario->world.field<mv::OccupancyTag>(agent.position) = true;
    tess::set_path_agent_goal(scenario->state, agent, spec.goal);
    scenario->agents.push_back(agent);
  }
  return scenario;
}

[[nodiscard]] inline bool within_bound(const mv::Outcome& outcome,
                                       int optimal) {
  const auto bound = std::max(3 * optimal, optimal + 8);
  return outcome.count(mv::Category::Arrived) == outcome.categories.size() &&
         outcome.ticks <= bound;
}

}  // namespace tess_test::movement::reciprocal
