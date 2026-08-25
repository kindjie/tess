// PR C4 Phase A (issue #253): conflict-local temporal escalation as
// harness-level test support. Detection, component extraction, a bounded
// complete local solver, and ordered execution -- no public API, no
// library change. The Phase A executor applies validated joint moves
// directly (with its own vertex/edge legality checks mirroring the C3
// oracle model) rather than through the production commit layer; that
// shortcut is declared in the pre-registration, and promotion to a
// public mechanism is Phase B's separate decision.
//
// Every parameter here is the pre-registered number, not a tunable:
// trigger K = 8 no-progress ticks, A_max = 6 agents, T_max = 32 region
// tiles, solver cap = 250,000 joint states (amendment 2 on the
// pre-registration reduced the original 2,000,000, verified
// outcome-identical on the full sweep). Exceeding any bound is
// `skipped`, a counted first-class outcome, never a truncated plan --
// partial or horizon-bounded planning is the pre-named WHCA failure
// mode and is structurally absent.
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <optional>
#include <unordered_set>
#include <vector>

#include "movement_scenarios.h"

namespace tess_test::movement {

struct EscalationParams {
  int trigger_ticks = 8;
  std::size_t max_agents = 6;
  std::size_t max_region_tiles = 32;
  std::size_t solver_state_cap = 250'000;
};

struct EscalationStats {
  std::uint64_t fired = 0;
  std::uint64_t skipped_bounds = 0;
  std::uint64_t skipped_unsolvable = 0;
  std::uint64_t aborted = 0;
  std::uint64_t plan_steps_executed = 0;
  std::uint64_t solver_states = 0;  // summed over fires
};

namespace escalation_detail {

// Deterministic dense tile set keyed by coordinate; iteration order is
// insertion order, so no unordered-container order ever decides anything.
struct Region {
  std::vector<tess::Coord3> tiles;

  [[nodiscard]] bool contains(tess::Coord3 c) const {
    return std::find_if(tiles.begin(), tiles.end(), [&](tess::Coord3 t) {
             return t.x == c.x && t.y == c.y;
           }) != tiles.end();
  }
  void add(tess::Coord3 c) {
    if (!contains(c)) {
      tiles.push_back(c);
    }
  }
};

// Bare-terrain BFS distance from `from` to `goal` (0xFFFFFFFF when
// unreachable), used only for the out-of-region objective rule.
[[nodiscard]] inline auto bare_distance(const Scenario& scenario,
                                        tess::Coord3 from, tess::Coord3 goal)
    -> std::uint32_t {
  const auto extent = scenario.options.extent;
  std::vector<std::uint32_t> dist(
      static_cast<std::size_t>(extent) * static_cast<std::size_t>(extent),
      0xFFFFFFFFu);
  const auto id = [extent](tess::Coord3 c) {
    return static_cast<std::size_t>(c.y) * static_cast<std::size_t>(extent) +
           static_cast<std::size_t>(c.x);
  };
  std::deque<tess::Coord3> frontier{goal};
  dist[id(goal)] = 0;
  while (!frontier.empty()) {
    const auto cur = frontier.front();
    frontier.pop_front();
    const auto d = dist[id(cur)];
    const std::array<tess::Coord3, 4> steps = {
        tess::Coord3{cur.x + 1, cur.y, 0}, tess::Coord3{cur.x - 1, cur.y, 0},
        tess::Coord3{cur.x, cur.y + 1, 0}, tess::Coord3{cur.x, cur.y - 1, 0}};
    for (const auto step : steps) {
      if (step.x < 0 || step.y < 0 || step.x >= extent || step.y >= extent) {
        continue;
      }
      if (!grid_at(scenario.terrain, extent, static_cast<int>(step.x),
                   static_cast<int>(step.y))) {
        continue;
      }
      if (dist[id(step)] != 0xFFFFFFFFu) continue;
      dist[id(step)] = d + 1;
      frontier.push_back(step);
    }
  }
  return dist[id(from)];
}

}  // namespace escalation_detail

// The pre-registered component and region fixed point. Seeds are the
// no-progress agents; each closure round adds any goal-holding agent
// whose position or retained-route next tile lies in the current
// region, then re-dilates. Agent order is index order throughout.
struct Component {
  std::vector<std::size_t> agents;  // index order
  escalation_detail::Region region;
  bool over_bounds = false;
};

[[nodiscard]] inline auto extract_component(
    const Scenario& scenario, const std::vector<std::size_t>& seeds,
    const EscalationParams& params, std::int64_t radius = 2) -> Component {
  Component out;
  const auto extent = scenario.options.extent;
  std::vector<bool> in_component(scenario.agents.size(), false);
  for (const auto seed : seeds) {
    in_component[seed] = true;
  }
  const auto rebuild_region = [&] {
    out.region.tiles.clear();
    for (std::size_t i = 0; i < scenario.agents.size(); ++i) {
      if (!in_component[i]) continue;
      const auto p = scenario.agents[i].position;
      for (std::int64_t dy = -radius; dy <= radius; ++dy) {
        for (std::int64_t dx = -radius; dx <= radius; ++dx) {
          if (std::abs(dx) + std::abs(dy) > radius) continue;
          const tess::Coord3 c{p.x + dx, p.y + dy, 0};
          if (c.x < 0 || c.y < 0 || c.x >= extent || c.y >= extent) continue;
          if (!grid_at(scenario.terrain, extent, static_cast<int>(c.x),
                       static_cast<int>(c.y))) {
            continue;
          }
          out.region.add(c);
        }
      }
      // A component agent's goal joins the region when it is near the
      // component (pre-registered distance 4).
      const auto& agent = scenario.agents[i];
      if (agent.has_goal) {
        const auto gd =
            std::abs(agent.goal.x - p.x) + std::abs(agent.goal.y - p.y);
        if (gd <= 4 &&
            grid_at(scenario.terrain, extent, static_cast<int>(agent.goal.x),
                    static_cast<int>(agent.goal.y))) {
          out.region.add(agent.goal);
        }
      }
    }
  };
  rebuild_region();
  bool changed = true;
  while (changed) {
    changed = false;
    for (std::size_t i = 0; i < scenario.agents.size(); ++i) {
      if (in_component[i]) continue;
      const auto& agent = scenario.agents[i];
      if (!agent.has_goal) continue;
      bool touches = out.region.contains(agent.position);
      if (!touches && i < scenario.state.routes.routes.size()) {
        const auto& route = scenario.state.routes.routes[i];
        if (!route.empty()) {
          touches = out.region.contains(route.front());
        }
      }
      if (touches) {
        in_component[i] = true;
        changed = true;
      }
    }
    if (changed) {
      rebuild_region();
    }
  }
  for (std::size_t i = 0; i < scenario.agents.size(); ++i) {
    if (in_component[i]) {
      out.agents.push_back(i);
    }
  }
  out.over_bounds = out.agents.size() > params.max_agents ||
                    out.region.tiles.size() > params.max_region_tiles;
  return out;
}

// Bounded complete local solver: joint-space BFS under the C3 oracle
// move semantics (vertex conflicts always; edge exchanges per the
// scenario's SwapPolicy; move-into-vacated legal), non-component agents
// as static obstacles, makespan-minimal. Objectives: an in-region goal
// must be reached exactly; an out-of-region goal maps to the set of
// region tiles whose bare-terrain distance to it is strictly below the
// agent's current tile's. Returns per-step joint positions, empty on
// skip (cap) or unsolvable.
struct LocalPlan {
  // steps[k][j] is the position of component agent j (component order)
  // after k+1 plan ticks.
  std::vector<std::vector<tess::Coord3>> steps;
  // Planning-time positions of every agent, so execution can distinguish
  // an outsider that WANDERED IN (invalidates the plan) from one --
  // settled agents especially -- that stood in the region all along and
  // was already modeled as a static obstacle.
  std::vector<tess::Coord3> planning_positions;
  std::size_t solver_states = 0;
  bool solved = false;
  bool capped = false;
};

[[nodiscard]] inline auto solve_component(const Scenario& scenario,
                                          const Component& component,
                                          const EscalationParams& params)
    -> LocalPlan {
  LocalPlan plan;
  plan.planning_positions.reserve(scenario.agents.size());
  for (const auto& agent : scenario.agents) {
    plan.planning_positions.push_back(agent.position);
  }
  const auto& tiles = component.region.tiles;
  const auto n = component.agents.size();
  const auto cell_of = [&](tess::Coord3 c) -> int {
    for (std::size_t t = 0; t < tiles.size(); ++t) {
      if (tiles[t].x == c.x && tiles[t].y == c.y) return static_cast<int>(t);
    }
    return -1;
  };
  // Static obstacles: every non-component agent standing in the region.
  std::vector<bool> blocked(tiles.size(), false);
  for (std::size_t i = 0; i < scenario.agents.size(); ++i) {
    if (std::find(component.agents.begin(), component.agents.end(), i) !=
        component.agents.end()) {
      continue;
    }
    const auto cell = cell_of(scenario.agents[i].position);
    if (cell >= 0) blocked[static_cast<std::size_t>(cell)] = true;
  }
  // Per-agent objective sets.
  std::vector<std::vector<bool>> objective(
      n, std::vector<bool>(tiles.size(), false));
  std::vector<int> start(n);
  for (std::size_t j = 0; j < n; ++j) {
    const auto& agent = scenario.agents[component.agents[j]];
    const auto s = cell_of(agent.position);
    if (s < 0) return plan;  // component invariant violated; treat unsolved
    start[j] = s;
    if (!agent.has_goal) {
      objective[j][static_cast<std::size_t>(s)] = true;
      continue;
    }
    const auto goal_cell = cell_of(agent.goal);
    if (goal_cell >= 0) {
      objective[j][static_cast<std::size_t>(goal_cell)] = true;
      continue;
    }
    const auto here =
        escalation_detail::bare_distance(scenario, agent.position, agent.goal);
    for (std::size_t t = 0; t < tiles.size(); ++t) {
      if (blocked[t]) continue;
      const auto d =
          escalation_detail::bare_distance(scenario, tiles[t], agent.goal);
      if (d < here) objective[j][t] = true;
    }
  }
  // Neighbor lists including stay, region-internal, unblocked.
  std::vector<std::vector<int>> moves(tiles.size());
  for (std::size_t t = 0; t < tiles.size(); ++t) {
    if (blocked[t]) continue;
    moves[t].push_back(static_cast<int>(t));
    const auto base = tiles[t];
    const std::array<tess::Coord3, 4> steps = {
        tess::Coord3{base.x + 1, base.y, 0},
        tess::Coord3{base.x - 1, base.y, 0},
        tess::Coord3{base.x, base.y + 1, 0},
        tess::Coord3{base.x, base.y - 1, 0}};
    for (const auto step : steps) {
      const auto id = cell_of(step);
      if (id >= 0 && !blocked[static_cast<std::size_t>(id)]) {
        moves[t].push_back(id);
      }
    }
  }
  const auto pack = [&](const std::vector<int>& state) {
    std::uint64_t key = 0;
    for (const auto cell : state) {
      key = key * tiles.size() + static_cast<std::uint64_t>(cell);
    }
    return key;
  };
  const auto unpack = [&](std::uint64_t key, std::vector<int>& state) {
    for (std::size_t j = n; j > 0; --j) {
      state[j - 1] = static_cast<int>(key % tiles.size());
      key /= tiles.size();
    }
  };
  const bool allow_swap = scenario.options.swap == tess::SwapPolicy::Permit;
  const auto satisfied = [&](const std::vector<int>& state) {
    for (std::size_t j = 0; j < n; ++j) {
      if (!objective[j][static_cast<std::size_t>(state[j])]) return false;
    }
    return true;
  };
  // Nodes hold only the packed key and a parent index; states are
  // decoded on demand. The full-state-vector representation measured as
  // the dominant cost of the whole substrate sweep (capped 2M-state
  // solves on 6-agent components copied an n-int vector per node).
  struct Node {
    std::uint64_t key = 0;
    std::size_t parent = 0;
    bool has_parent = false;
  };
  std::vector<Node> nodes;
  std::unordered_set<std::uint64_t> seen;
  nodes.push_back({pack(start), 0, false});
  seen.insert(nodes.front().key);
  std::size_t head = 0;
  std::size_t found = 0;
  bool solved = false;
  std::vector<int> state(n);
  std::vector<int> next(n);
  while (head < nodes.size()) {
    unpack(nodes[head].key, state);
    if (satisfied(state)) {
      found = head;
      solved = true;
      break;
    }
    if (seen.size() > params.solver_state_cap) {
      plan.capped = true;
      plan.solver_states = seen.size();
      return plan;
    }
    const auto compose = [&](auto&& self, std::size_t agent) -> void {
      if (agent == n) {
        const auto key = pack(next);
        if (seen.insert(key).second) {
          nodes.push_back({key, head, true});
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
    ++head;
  }
  plan.solver_states = seen.size();
  if (!solved) {
    return plan;
  }
  // Unwind, oldest step first, skipping the start state.
  std::vector<std::size_t> chain;
  for (auto at = found; nodes[at].has_parent; at = nodes[at].parent) {
    chain.push_back(at);
  }
  std::reverse(chain.begin(), chain.end());
  for (const auto at : chain) {
    unpack(nodes[at].key, state);
    std::vector<tess::Coord3> step;
    step.reserve(n);
    for (const auto cell : state) {
      step.push_back(tiles[static_cast<std::size_t>(cell)]);
    }
    plan.steps.push_back(std::move(step));
  }
  plan.solved = true;
  return plan;
}

// Amendment 1 trigger 2: would settling `candidate_goal` make any other
// live agent's goal unreachable under the terminal set plus that tile?
// Returns the stranded agents (agent-index order), empty when safe.
[[nodiscard]] inline auto stranded_by_settle(const Scenario& scenario,
                                             std::size_t arriving,
                                             tess::Coord3 candidate_goal)
    -> std::vector<std::size_t> {
  std::vector<std::size_t> stranded;
  const auto extent = scenario.options.extent;
  const auto id = [extent](tess::Coord3 c) {
    return static_cast<std::size_t>(c.y) * static_cast<std::size_t>(extent) +
           static_cast<std::size_t>(c.x);
  };
  // Blocked = terrain walls, settled tiles, and the candidate settle.
  std::vector<std::uint8_t> open(
      static_cast<std::size_t>(extent) * static_cast<std::size_t>(extent), 0);
  for (int y = 0; y < extent; ++y) {
    for (int x = 0; x < extent; ++x) {
      open[static_cast<std::size_t>(y) * static_cast<std::size_t>(extent) +
           static_cast<std::size_t>(x)] =
          grid_at(scenario.terrain, extent, x, y) ? 1 : 0;
    }
  }
  for (std::size_t i = 0; i < scenario.agents.size(); ++i) {
    const auto& other = scenario.agents[i];
    if (!other.has_goal) {
      open[id(other.position)] = 0;  // terminal agents block
    }
  }
  open[id(candidate_goal)] = 0;
  for (std::size_t i = 0; i < scenario.agents.size(); ++i) {
    if (i == arriving) continue;
    const auto& agent = scenario.agents[i];
    if (!agent.has_goal) continue;
    if (agent.goal.x == candidate_goal.x && agent.goal.y == candidate_goal.y) {
      stranded.push_back(i);
      continue;
    }
    // BFS from the agent under the augmented blocked set.
    std::vector<std::uint8_t> seen(open.size(), 0);
    std::deque<tess::Coord3> frontier{agent.position};
    seen[id(agent.position)] = 1;
    bool reachable = false;
    while (!frontier.empty() && !reachable) {
      const auto cur = frontier.front();
      frontier.pop_front();
      if (cur.x == agent.goal.x && cur.y == agent.goal.y) {
        reachable = true;
        break;
      }
      const std::array<tess::Coord3, 4> steps = {
          tess::Coord3{cur.x + 1, cur.y, 0}, tess::Coord3{cur.x - 1, cur.y, 0},
          tess::Coord3{cur.x, cur.y + 1, 0}, tess::Coord3{cur.x, cur.y - 1, 0}};
      for (const auto step : steps) {
        if (step.x < 0 || step.y < 0 || step.x >= extent || step.y >= extent) {
          continue;
        }
        if (open[id(step)] == 0 &&
            !(step.x == agent.goal.x && step.y == agent.goal.y)) {
          continue;
        }
        if (seen[id(step)] != 0) continue;
        seen[id(step)] = 1;
        frontier.push_back(step);
      }
    }
    if (!reachable) {
      stranded.push_back(i);
    }
  }
  return stranded;
}

// Settle loop with escalation armed: identical to settle_with_pibt except
// that (a) per-agent no-progress counters feed the pre-registered
// trigger, (b) a solved plan executes its joint steps EXCLUSIVELY (other
// agents hold; the C3 fixtures are all-component, so this Phase A
// simplification is not exercised by mixed populations there), and (c)
// every planned step is legality-re-checked at execution time -- any
// failure aborts the whole plan and re-arms detection.
template <typename Ranking>
[[nodiscard]] inline auto settle_with_pibt_escalation(
    Scenario& scenario, Ranking&& rank, EscalationStats* stats_out = nullptr,
    EscalationParams params = {}) -> Outcome {
  Outcome outcome;
  EscalationStats stats;
  tess::PathRequestRuntime runtime;
  runtime.reserve_requests(128);
  runtime.reserve_search_nodes(16384);
  runtime.reserve_path_nodes(65536);
  tess::JointMoveScratch scratch;
  scratch.reserve(scenario.agents.size());
  tess::PibtPriorities priorities;
  priorities.reserve(scenario.agents.size());

  auto options = tess::PathAgentTickOptions{};
  options.max_blocked_retries = scenario.options.max_blocked_retries;
  options.blocked_exhaustion_policy = scenario.options.exhaustion_policy;
  TESS_ASSERT(options.max_steps == 1);
  const auto move_options = tess::JointMoveOptions{scenario.options.swap};

  const auto extent = scenario.options.extent;
  std::vector<int> no_progress(scenario.agents.size(), 0);
  Component active_component;
  LocalPlan active_plan;
  std::size_t plan_cursor = 0;
  bool plan_live = false;

  int cooldown = 0;
  struct SealMemo {
    bool valid = false;
    bool dangerous = false;
    tess::Coord3 position{};
    tess::Coord3 goal{};
    std::size_t terminal_count = 0;
    std::vector<std::size_t> stranded;
  };
  std::vector<SealMemo> seal_memo(scenario.agents.size());
  // Futility memo: a component whose exact configuration (members,
  // their positions, the terminal count, the radius) already failed to
  // solve would fail identically again -- the solve is deterministic --
  // so repeated wedges must not re-burn the state cap every cooldown
  // window. This is a pure cost optimization with provably identical
  // semantics, and it is what makes the substrate sweep affordable.
  std::unordered_set<std::uint64_t> futile;
  const auto futility_key = [&](const Component& component, std::int64_t radius,
                                std::size_t terminal_count) {
    std::uint64_t hash =
        0xCBF29CE484222325ULL ^ static_cast<std::uint64_t>(radius);
    const auto mix = [&hash](std::uint64_t value) {
      hash ^= value + 0x9E3779B97F4A7C15ULL + (hash << 6U) + (hash >> 2U);
    };
    mix(terminal_count);
    for (const auto index : component.agents) {
      mix(index);
      mix(static_cast<std::uint64_t>(scenario.agents[index].position.x));
      mix(static_cast<std::uint64_t>(scenario.agents[index].position.y));
    }
    return hash;
  };
  const auto abort_plan = [&] {
    plan_live = false;
    plan_cursor = 0;
    ++stats.aborted;
    cooldown = params.trigger_ticks;
    std::fill(no_progress.begin(), no_progress.end(), 0);
  };

  // Amendment 1 rule 1: radius 2, then 4 on unsolvable or capped, then
  // skip. Completion-planning at every attempt, never truncation.
  const auto attempt_escalation = [&](const std::vector<std::size_t>& seeds) {
    std::size_t terminal_count = 0;
    for (const auto& agent : scenario.agents) {
      if (!agent.has_goal) ++terminal_count;
    }
    for (const std::int64_t radius : {std::int64_t{2}, std::int64_t{4}}) {
      auto component = extract_component(scenario, seeds, params, radius);
      if (component.over_bounds) {
        ++stats.skipped_bounds;
        break;
      }
      const auto key = futility_key(component, radius, terminal_count);
      if (futile.contains(key)) {
        continue;
      }
      auto plan = solve_component(scenario, component, params);
      stats.solver_states += plan.solver_states;
      if (plan.solved && !plan.steps.empty()) {
        ++stats.fired;
        active_component = std::move(component);
        active_plan = std::move(plan);
        plan_cursor = 0;
        plan_live = true;
        return;
      }
      futile.insert(key);
      if (radius == 4) {
        if (plan.capped) {
          ++stats.skipped_bounds;
        } else {
          ++stats.skipped_unsolvable;
        }
      }
    }
    std::fill(no_progress.begin(), no_progress.end(), 0);
  };

  std::vector<tess::Coord3> previous;
  int stalled = 0;
  int tick = 0;
  for (; tick < scenario.options.tick_cap; ++tick) {
    if (std::all_of(
            scenario.agents.begin(), scenario.agents.end(),
            [](const tess::PathAgentState& a) { return !a.has_goal; })) {
      outcome.fixpoint = true;
      break;
    }
    if (refresh_settled(scenario)) {
      tess::mark_pathing_dirty(scenario.state);
    }

    if (cooldown > 0) {
      --cooldown;
    }
    if (!plan_live && cooldown == 0) {
      // Amendment 1 trigger 2: an imminent arrival that would strand a
      // live agent escalates immediately, seeded with the arriver and
      // everyone its settle would strand -- a seal can only be
      // prevented, never repaired, under terminal-set monotonicity.
      // Memoized: a blocked agent sitting beside its goal re-poses the
      // identical question every tick, so the reachability probe reruns
      // only when the agent's position or goal or the terminal count
      // changed. Deterministic, and it collapses the detector from a
      // per-tick full-map sweep to a per-change one.
      std::size_t terminal_count = 0;
      for (const auto& agent : scenario.agents) {
        if (!agent.has_goal) ++terminal_count;
      }
      std::vector<std::size_t> seal_seeds;
      for (std::size_t i = 0; i < scenario.agents.size() && seal_seeds.empty();
           ++i) {
        const auto& agent = scenario.agents[i];
        if (!agent.has_goal) continue;
        const auto d = std::abs(agent.position.x - agent.goal.x) +
                       std::abs(agent.position.y - agent.goal.y);
        if (d != 1) continue;
        auto& memo = seal_memo[i];
        const bool memo_hit =
            memo.valid && memo.position.x == agent.position.x &&
            memo.position.y == agent.position.y &&
            memo.goal.x == agent.goal.x && memo.goal.y == agent.goal.y &&
            memo.terminal_count == terminal_count;
        if (memo_hit && !memo.dangerous) continue;
        const auto stranded = memo_hit
                                  ? memo.stranded
                                  : stranded_by_settle(scenario, i, agent.goal);
        memo.valid = true;
        memo.position = agent.position;
        memo.goal = agent.goal;
        memo.terminal_count = terminal_count;
        memo.dangerous = !stranded.empty();
        memo.stranded = stranded;
        if (!stranded.empty()) {
          seal_seeds.push_back(i);
          for (const auto victim : stranded) {
            seal_seeds.push_back(victim);
          }
          std::sort(seal_seeds.begin(), seal_seeds.end());
        }
      }
      if (!seal_seeds.empty()) {
        attempt_escalation(seal_seeds);
        if (!plan_live) {
          // A failed seal-prevention attempt must not retry every tick;
          // the cooldown bounds the solve cost and lets the tier move.
          cooldown = params.trigger_ticks;
        }
      }
    }

    bool ran_plan_step = false;
    if (plan_live) {
      // Execute one exclusive plan step with full legality re-checks.
      const auto& step = active_plan.steps[plan_cursor];
      bool legal = true;
      // An outsider invalidates the plan only by MOVING into the region
      // after planning; agents (settled ones especially) that stood
      // there at planning time were already static obstacles in the
      // solve.
      for (std::size_t i = 0; i < scenario.agents.size() && legal; ++i) {
        if (std::find(active_component.agents.begin(),
                      active_component.agents.end(),
                      i) != active_component.agents.end()) {
          continue;
        }
        const auto& pos = scenario.agents[i].position;
        if (!active_component.region.contains(pos)) continue;
        const auto& was = active_plan.planning_positions[i];
        legal = pos.x == was.x && pos.y == was.y;
      }
      for (std::size_t j = 0; j < step.size() && legal; ++j) {
        const auto target = step[j];
        legal = grid_at(scenario.terrain, extent, static_cast<int>(target.x),
                        static_cast<int>(target.y)) &&
                !scenario.world.template field<SettledTag>(target);
        const auto from = scenario.agents[active_component.agents[j]].position;
        for (std::size_t k = 0; k < step.size() && legal; ++k) {
          if (k == j) continue;
          if (step[k].x == target.x && step[k].y == target.y) legal = false;
          if (scenario.options.swap != tess::SwapPolicy::Permit) {
            const auto from_k =
                scenario.agents[active_component.agents[k]].position;
            if (step[k].x == from.x && step[k].y == from.y &&
                target.x == from_k.x && target.y == from_k.y) {
              legal = false;
            }
          }
        }
      }
      if (!legal) {
        abort_plan();
        // Fall through to a normal tier tick below: an aborted plan must
        // not cost the population a motionless tick, or a fire/abort
        // cycle starves everyone into the wedge rule.
      } else {
        ran_plan_step = true;
        for (std::size_t j = 0; j < step.size(); ++j) {
          auto& agent = scenario.agents[active_component.agents[j]];
          if (agent.position.x == step[j].x && agent.position.y == step[j].y) {
            continue;
          }
          scenario.world.template field<OccupancyTag>(agent.position) = false;
          agent.position = step[j];
          scenario.world.template field<OccupancyTag>(agent.position) = true;
        }
        ++stats.plan_steps_executed;
        ++plan_cursor;
        if (plan_cursor == active_plan.steps.size()) {
          plan_live = false;
          plan_cursor = 0;
          std::fill(no_progress.begin(), no_progress.end(), 0);
          // Externally-moved agents have stale retained routes; force
          // replanning from the new configuration.
          tess::mark_pathing_dirty(scenario.state);
        }
      }
    }
    if (!ran_plan_step) {
      tess::JointMoveStats move_stats;
      (void)tess::tick_weighted_path_agents_with_pibt<
          World, Traveler, 4u, OccupancyTag, ReservationTag>(
          scenario.state, scenario.world, scenario.agents, runtime, priorities,
          scratch, rank, options, move_options, nullptr, &move_stats);
      outcome.swaps += move_stats.swaps;
      outcome.swaps_denied += move_stats.swaps_denied;
    }

    std::vector<tess::Coord3> current;
    current.reserve(scenario.agents.size());
    for (const auto& agent : scenario.agents) {
      current.push_back(agent.position);
    }
    if (!previous.empty()) {
      for (std::size_t i = 0; i < current.size(); ++i) {
        const auto moved =
            current[i].x != previous[i].x || current[i].y != previous[i].y;
        if (moved || !scenario.agents[i].has_goal) {
          no_progress[i] = 0;
        } else {
          ++no_progress[i];
        }
      }
    }
    stalled = (current == previous) ? stalled + 1 : 0;
    previous = std::move(current);

    if (!plan_live && cooldown == 0) {
      std::vector<std::size_t> seeds;
      for (std::size_t i = 0; i < scenario.agents.size(); ++i) {
        if (scenario.agents[i].has_goal &&
            no_progress[i] >= params.trigger_ticks) {
          seeds.push_back(i);
        }
      }
      if (!seeds.empty()) {
        attempt_escalation(seeds);
      }
    }

    if (stalled >= scenario.options.wedge_ticks) {
      outcome.fixpoint = true;
      ++tick;
      break;
    }
  }
  outcome.ticks = tick;
  outcome.censored = !outcome.fixpoint;
  outcome.categories = classify(scenario, outcome.censored);
  outcome.structural_seals = structural_seal_count(scenario);
  if (stats_out != nullptr) {
    *stats_out = stats;
  }
  return outcome;
}

}  // namespace tess_test::movement
