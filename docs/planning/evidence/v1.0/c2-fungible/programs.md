# C2 measurement program: recorded source

One program, three interleaved arms, one binary, compiled against the
merged pool fixture. Captured output is `arms.txt` alongside.

## c2_arms.cc

```cpp
// C2 fungible-goals screen: three arms on identical pool instances.
//
//   A: greedy one-shot dispatch (global min edge, ties by agent then goal)
//   B: optimal one-shot dispatch (Hungarian, successive shortest paths)
//   C: B's assignment + the pre-registered single-move in-movement
//      reassignment policy in the settle loop's before_tick hook
//
// Pre-registered in issue #241 (revision 2 + amendment 1). Acceptance is
// candidate vs Control B: paired geometric mean of ticks-to-fixpoint,
// >= 8% in the candidate's favour AND bootstrap CI excluding 1.0.
// Compile against the merged pool fixture; this program is evidence
// source, never merged.
#include "movement_scenarios.h"

#include <cmath>
#include <cstdio>
#include <deque>
#include <map>
#include <string>
#include <tuple>
#include <vector>

namespace {

namespace mv = tess_test::movement;

constexpr std::uint32_t kUnreachable = 0x3FFFFFFF;

int failures = 0;
#define CHECK(cond, msg)                                              \
  do {                                                                \
    if (!(cond)) {                                                    \
      ++failures;                                                     \
      std::printf("GATE FAIL line %d: %s (%s)\n", __LINE__, #cond, msg); \
    }                                                                 \
  } while (0)

// ---- Distance fields: BFS from each pool goal over bare terrain. ----
// This is the single distance the amendment pins for every arm: dispatch
// cost, reassignment cost, and contention all read the same fields.
struct DistanceFields {
  int extent = 0;
  std::vector<std::vector<std::uint32_t>> per_goal;  // [goal][y*extent+x]

  [[nodiscard]] auto at(std::size_t goal, tess::Coord3 pos) const
      -> std::uint32_t {
    return per_goal[goal][static_cast<std::size_t>(pos.y) *
                              static_cast<std::size_t>(extent) +
                          static_cast<std::size_t>(pos.x)];
  }
};

DistanceFields build_fields(const mv::PoolScenario& pool) {
  DistanceFields fields;
  const auto extent = pool.scenario->options.extent;
  fields.extent = extent;
  const auto& terrain = pool.scenario->terrain;
  const auto cells = static_cast<std::size_t>(extent) *
                     static_cast<std::size_t>(extent);
  fields.per_goal.assign(pool.pool.size(),
                         std::vector<std::uint32_t>(cells, kUnreachable));
  std::deque<std::pair<int, int>> frontier;
  for (std::size_t g = 0; g < pool.pool.size(); ++g) {
    auto& dist = fields.per_goal[g];
    const auto goal = pool.pool[g];
    frontier.clear();
    dist[static_cast<std::size_t>(goal.y) * extent + goal.x] = 0;
    frontier.emplace_back(static_cast<int>(goal.x),
                          static_cast<int>(goal.y));
    while (!frontier.empty()) {
      const auto [x, y] = frontier.front();
      frontier.pop_front();
      const auto d = dist[static_cast<std::size_t>(y) * extent + x];
      const int steps[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
      for (const auto& step : steps) {
        const int nx = x + step[0];
        const int ny = y + step[1];
        if (nx < 0 || ny < 0 || nx >= extent || ny >= extent) continue;
        if (!mv::grid_at(terrain, extent, nx, ny)) continue;
        auto& cell = dist[static_cast<std::size_t>(ny) * extent + nx];
        if (cell != kUnreachable) continue;
        cell = d + 1;
        frontier.emplace_back(nx, ny);
      }
    }
  }
  return fields;
}

// ---- Control A: greedy global-min-edge dispatch. ----
std::vector<std::size_t> assign_greedy(const mv::PoolScenario& pool,
                                       const DistanceFields& fields) {
  const auto n = pool.scenario->agents.size();
  const auto m = pool.pool.size();
  std::vector<std::size_t> assignment(n, m);
  std::vector<bool> agent_done(n, false), goal_done(m, false);
  for (std::size_t round = 0; round < n; ++round) {
    std::uint64_t best = ~0ULL;
    std::size_t ba = n, bg = m;
    for (std::size_t a = 0; a < n; ++a) {
      if (agent_done[a]) continue;
      for (std::size_t g = 0; g < m; ++g) {
        if (goal_done[g]) continue;
        const auto c = static_cast<std::uint64_t>(
            fields.at(g, pool.scenario->agents[a].position));
        if (c < best) {
          best = c;
          ba = a;
          bg = g;
        }
      }
    }
    assignment[ba] = bg;
    agent_done[ba] = true;
    goal_done[bg] = true;
  }
  return assignment;
}

// ---- Control B: optimal rectangular assignment (successive shortest
// augmenting paths with potentials; deterministic). ----
std::vector<std::size_t> assign_optimal(const mv::PoolScenario& pool,
                                        const DistanceFields& fields) {
  const auto n = pool.scenario->agents.size();
  const auto m = pool.pool.size();
  const std::int64_t inf = std::int64_t{1} << 60;
  // 1-indexed e-maxx formulation: rows are agents, columns goals.
  std::vector<std::int64_t> u(n + 1, 0), v(m + 1, 0);
  std::vector<std::size_t> way(m + 1, 0), col_owner(m + 1, 0);
  const auto cost = [&](std::size_t a, std::size_t g) -> std::int64_t {
    return static_cast<std::int64_t>(
        fields.at(g - 1, pool.scenario->agents[a - 1].position));
  };
  for (std::size_t i = 1; i <= n; ++i) {
    col_owner[0] = i;
    std::size_t j0 = 0;
    std::vector<std::int64_t> minv(m + 1, inf);
    std::vector<bool> used(m + 1, false);
    do {
      used[j0] = true;
      const std::size_t i0 = col_owner[j0];
      std::int64_t delta = inf;
      std::size_t j1 = 0;
      for (std::size_t j = 1; j <= m; ++j) {
        if (used[j]) continue;
        const auto cur = cost(i0, j) - u[i0] - v[j];
        if (cur < minv[j]) {
          minv[j] = cur;
          way[j] = j0;
        }
        if (minv[j] < delta) {
          delta = minv[j];
          j1 = j;
        }
      }
      for (std::size_t j = 0; j <= m; ++j) {
        if (used[j]) {
          u[col_owner[j]] += delta;
          v[j] -= delta;
        } else {
          minv[j] -= delta;
        }
      }
      j0 = j1;
    } while (col_owner[j0] != 0);
    do {
      const auto j1 = way[j0];
      col_owner[j0] = col_owner[j1];
      j0 = j1;
    } while (j0 != 0);
  }
  std::vector<std::size_t> assignment(n, m);
  for (std::size_t j = 1; j <= m; ++j) {
    if (col_owner[j] != 0) assignment[col_owner[j] - 1] = j - 1;
  }
  return assignment;
}

// ---- The pre-registered reassignment policy (amendment 1). ----
// Candidate moves: (live agent, unheld unconsumed goal) plus pairwise
// exchanges of held goals between live agents. Single best
// strictly-improving move per tick, improvement must exceed one tile,
// scan order pinned: agent index then pool order for moves, (lower,
// higher) agent index for exchanges. Applied through
// set_path_agent_goal only, so every action surfaces as supersession.
struct PolicyState {
  std::vector<std::size_t> held_goal;   // per agent, m == none
  std::vector<std::size_t> holder;      // per goal, n == none
  std::vector<bool> consumed;           // arrived agent sits on it
  std::uint64_t applied_calls = 0;      // set_path_agent_goal count
  std::uint64_t acting_ticks = 0;       // ticks with an applied move
  std::uint64_t contended_ticks = 0;    // ticks a better matching exists
  std::uint64_t ticks_seen = 0;
};

struct BestMove {
  bool found = false;
  bool exchange = false;
  std::size_t agent_a = 0, agent_b = 0, goal = 0;
  std::int64_t reduction = 0;
};

BestMove find_best_move(const mv::Scenario& scenario,
                        const DistanceFields& fields,
                        const PolicyState& ps) {
  BestMove best;
  const auto n = scenario.agents.size();
  const auto m = ps.consumed.size();
  const auto dist_or_inf = [&](std::size_t g, tess::Coord3 pos)
      -> std::int64_t { return fields.at(g, pos); };
  // Moves to unheld, unconsumed goals.
  for (std::size_t a = 0; a < n; ++a) {
    const auto& agent = scenario.agents[a];
    if (!agent.has_goal || ps.held_goal[a] == m) continue;
    const auto cur = dist_or_inf(ps.held_goal[a], agent.position);
    for (std::size_t g = 0; g < m; ++g) {
      if (ps.holder[g] != n || ps.consumed[g]) continue;
      const auto red = cur - dist_or_inf(g, agent.position);
      if (red > best.reduction) {
        best = BestMove{true, false, a, 0, g, red};
      }
    }
  }
  // Pairwise exchanges.
  for (std::size_t a = 0; a < n; ++a) {
    const auto& agent_a = scenario.agents[a];
    if (!agent_a.has_goal || ps.held_goal[a] == m) continue;
    for (std::size_t b = a + 1; b < n; ++b) {
      const auto& agent_b = scenario.agents[b];
      if (!agent_b.has_goal || ps.held_goal[b] == m) continue;
      const auto ga = ps.held_goal[a];
      const auto gb = ps.held_goal[b];
      const auto before = dist_or_inf(ga, agent_a.position) +
                          dist_or_inf(gb, agent_b.position);
      const auto after = dist_or_inf(gb, agent_a.position) +
                         dist_or_inf(ga, agent_b.position);
      const auto red = before - after;
      if (red > best.reduction) {
        best = BestMove{true, true, a, b, gb, red};
      }
    }
  }
  // "Strictly better by more than one tile."
  if (best.reduction <= 1) best.found = false;
  return best;
}

// ---- One arm run. ----
enum class Arm { Greedy, Optimal, Candidate };

struct RunResult {
  mv::Outcome outcome;
  std::uint64_t reassignment_calls = 0;
  std::uint64_t acting_ticks = 0;
  std::uint64_t contended_ticks = 0;
  std::uint64_t ticks_seen = 0;
  tess::diagnostics::FlowCounters flow{};
  std::uint64_t position_digest = 0;
};

RunResult run_arm(mv::Family family, unsigned trial, std::size_t factor,
                  Arm arm) {
  auto pool = mv::build_pool_scenario(family, trial, factor);
  auto& scenario = *pool.scenario;
  const auto fields = build_fields(pool);
  const auto n = scenario.agents.size();
  const auto m = pool.pool.size();

  tess::diagnostics::FlowAccounting accounting;
  scenario.state.flow_accounting = &accounting;

  const auto assignment = arm == Arm::Greedy ? assign_greedy(pool, fields)
                                             : assign_optimal(pool, fields);
  PolicyState ps;
  ps.held_goal.assign(n, m);
  ps.holder.assign(m, n);
  ps.consumed.assign(m, false);
  for (std::size_t a = 0; a < n; ++a) {
    const auto g = assignment[a];
    tess::set_path_agent_goal(scenario.state, scenario.agents[a],
                              pool.pool[g]);
    ps.held_goal[a] = g;
    ps.holder[g] = a;
  }

  std::uint64_t tick = 0;
  const auto ranking = mv::route_attachment_ranking(scenario);
  auto outcome = mv::settle_with_pibt(
      scenario, ranking, [&](mv::Scenario& s) {
        tess::observe_path_agent_flow_tick(
            s.state, std::span<const tess::PathAgentState>(s.agents), ++tick);
        // Release arrived agents' goals as consumed so they are never
        // offered again (the GoalOccupied defect gate).
        for (std::size_t a = 0; a < n; ++a) {
          if (!s.agents[a].has_goal && ps.held_goal[a] != m) {
            ps.consumed[ps.held_goal[a]] = true;
            ps.holder[ps.held_goal[a]] = n;
            ps.held_goal[a] = m;
          }
        }
        // Assignment validity, checked every tick in every arm.
        for (std::size_t g = 0; g < m; ++g) {
          if (ps.holder[g] != n) {
            CHECK(ps.held_goal[ps.holder[g]] == g, "holder map incoherent");
          }
        }
        ++ps.ticks_seen;
        const auto best = find_best_move(s, fields, ps);
        if (best.found) ++ps.contended_ticks;
        if (arm != Arm::Candidate || !best.found) return;
        ++ps.acting_ticks;
        if (best.exchange) {
          const auto ga = ps.held_goal[best.agent_a];
          const auto gb = ps.held_goal[best.agent_b];
          tess::set_path_agent_goal(s.state, s.agents[best.agent_a],
                                    pool.pool[gb]);
          tess::set_path_agent_goal(s.state, s.agents[best.agent_b],
                                    pool.pool[ga]);
          ps.applied_calls += 2;
          ps.held_goal[best.agent_a] = gb;
          ps.held_goal[best.agent_b] = ga;
          ps.holder[ga] = best.agent_b;
          ps.holder[gb] = best.agent_a;
        } else {
          const auto ga = ps.held_goal[best.agent_a];
          tess::set_path_agent_goal(s.state, s.agents[best.agent_a],
                                    pool.pool[best.goal]);
          ps.applied_calls += 1;
          ps.holder[ga] = n;
          ps.held_goal[best.agent_a] = best.goal;
          ps.holder[best.goal] = best.agent_a;
        }
      });

  RunResult out;
  out.reassignment_calls = ps.applied_calls;
  out.acting_ticks = ps.acting_ticks;
  out.contended_ticks = ps.contended_ticks;
  out.ticks_seen = ps.ticks_seen;
  out.flow = accounting.counters;
  std::uint64_t digest = 0xCBF29CE484222325ULL;
  const auto mix = [&digest](std::uint64_t value) {
    digest ^= value + 0x9E3779B97F4A7C15ULL + (digest << 6U) + (digest >> 2U);
  };
  for (const auto& agent : scenario.agents) {
    mix(static_cast<std::uint64_t>(agent.position.x));
    mix(static_cast<std::uint64_t>(agent.position.y));
  }
  for (const auto category : outcome.categories) {
    mix(static_cast<std::uint64_t>(category));
  }
  mix(static_cast<std::uint64_t>(outcome.ticks));
  out.position_digest = digest;

  // Gates evaluated at quiescence, per run.
  const char* tag = arm == Arm::Greedy ? "A" : arm == Arm::Optimal ? "B" : "C";
  CHECK(outcome.count(mv::Category::GoalOccupied) == 0, tag);
  const auto snap_ok = out.flow.admission_identity_holds();
  CHECK(snap_ok, tag);
  CHECK(out.flow.retention_identity_holds(), tag);
  const auto expected_offered = n + ps.applied_calls;
  CHECK(out.flow.offered == expected_offered, tag);
  CHECK(out.flow.superseded == ps.applied_calls, tag);
  CHECK(out.flow.completed == outcome.count(mv::Category::Arrived), tag);
  out.outcome = std::move(outcome);
  return out;
}

// ---- Severity, exclusion, and the paired statistic. ----
int severity(mv::Category category) {
  switch (category) {
    case mv::Category::Arrived:
      return 0;
    case mv::Category::Wedged:
    case mv::Category::GoalOccupied:
      return 1;
    case mv::Category::Sealed:
      return 2;
    case mv::Category::Censored:
      return 3;
  }
  return 3;
}

bool severity_changed(const mv::Outcome& lhs, const mv::Outcome& rhs) {
  if (lhs.categories.size() != rhs.categories.size()) return true;
  for (std::size_t i = 0; i < lhs.categories.size(); ++i) {
    if (severity(lhs.categories[i]) != severity(rhs.categories[i])) {
      return true;
    }
  }
  return false;
}

struct Cell {
  std::vector<double> log_ratios;     // log(candidate/optimal) per seed
  std::vector<double> log_ratios_ab;  // log(greedy/optimal), same rule
  int excluded = 0;
  int seeds = 0;
  std::uint64_t contended_b = 0, ticks_b = 0;
  std::uint64_t acting_c = 0, calls_c = 0, ticks_c = 0;
  std::uint64_t contended_a = 0, ticks_a = 0;
  long long residual_delta = 0;  // (C non-arrived) - (B non-arrived)
};

double geo_mean(const std::vector<double>& logs) {
  double sum = 0;
  for (const auto v : logs) sum += v;
  return std::exp(sum / static_cast<double>(logs.size()));
}

// Bootstrap CI over seeds (fixed seed; 2000 resamples; percentile 2.5/97.5).
std::pair<double, double> bootstrap_ci(const std::vector<double>& logs) {
  mv::SplitMix64 rng(0xC2C2C2C2C2C2C2C2ULL);
  std::vector<double> means;
  means.reserve(2000);
  std::vector<double> sample(logs.size());
  for (int rep = 0; rep < 2000; ++rep) {
    for (std::size_t i = 0; i < logs.size(); ++i) {
      sample[i] = logs[rng.below(logs.size())];
    }
    means.push_back(geo_mean(sample));
  }
  std::sort(means.begin(), means.end());
  return {means[static_cast<std::size_t>(0.025 * 2000)],
          means[static_cast<std::size_t>(0.975 * 2000) - 1]};
}

}  // namespace

int main() {
  constexpr std::array<mv::Family, 7> kFamilies = {
      mv::Family::Warehouse,    mv::Family::Ring,
      mv::Family::Colony,       mv::Family::RandomSparse,
      mv::Family::RandomMedium, mv::Family::RandomDense,
      mv::Family::Adversarial,
  };
  std::vector<double> pooled_logs;
  std::vector<double> pooled_logs_strict;  // drops uninterpretable cells
  std::vector<double> pooled_logs_ab;
  std::vector<double> per_factor_logs[3];
  std::uint64_t total_contended_b = 0, total_ticks_b = 0;
  int total_excluded = 0, total_seeds = 0;
  bool any_family_uninterpretable = false;

  for (const auto family : kFamilies) {
    for (std::size_t factor = 0; factor < 3; ++factor) {
      Cell cell;
      for (unsigned trial = 0; trial < mv::trial_count(family); ++trial) {
        const auto a = run_arm(family, trial, factor, Arm::Greedy);
        const auto b = run_arm(family, trial, factor, Arm::Optimal);
        const auto c = run_arm(family, trial, factor, Arm::Candidate);
        // Determinism: bit-identical replay per seed for EVERY arm, as
        // pre-registered.
        const auto a2 = run_arm(family, trial, factor, Arm::Greedy);
        const auto b2 = run_arm(family, trial, factor, Arm::Optimal);
        const auto c2 = run_arm(family, trial, factor, Arm::Candidate);
        CHECK(a.position_digest == a2.position_digest, "replay A");
        CHECK(b.position_digest == b2.position_digest, "replay B");
        CHECK(c.position_digest == c2.position_digest, "replay C");

        ++cell.seeds;
        cell.contended_a += a.contended_ticks;
        cell.ticks_a += a.ticks_seen;
        cell.contended_b += b.contended_ticks;
        cell.ticks_b += b.ticks_seen;
        cell.acting_c += c.acting_ticks;
        cell.calls_c += c.reassignment_calls;
        cell.ticks_c += c.ticks_seen;
        const auto residual = [](const mv::Outcome& o) {
          return static_cast<long long>(o.categories.size() -
                                        o.count(mv::Category::Arrived));
        };
        cell.residual_delta += residual(c.outcome) - residual(b.outcome);
        if (!a.outcome.censored && !b.outcome.censored &&
            !severity_changed(a.outcome, b.outcome)) {
          cell.log_ratios_ab.push_back(
              std::log(static_cast<double>(a.outcome.ticks) /
                       static_cast<double>(b.outcome.ticks)));
        }
        if (c.outcome.censored || b.outcome.censored ||
            severity_changed(c.outcome, b.outcome)) {
          ++cell.excluded;
          continue;
        }
        const auto ratio = static_cast<double>(c.outcome.ticks) /
                           static_cast<double>(b.outcome.ticks);
        cell.log_ratios.push_back(std::log(ratio));
      }
      const auto excluded_frac =
          static_cast<double>(cell.excluded) / cell.seeds;
      const auto uninterpretable = excluded_frac > 0.20;
      any_family_uninterpretable |= uninterpretable;
      total_excluded += cell.excluded;
      total_seeds += cell.seeds;
      total_contended_b += cell.contended_b;
      total_ticks_b += cell.ticks_b;
      const auto gm =
          cell.log_ratios.empty() ? 1.0 : geo_mean(cell.log_ratios);
      std::printf(
          "%-14s M=%-4zu seeds=%2d excl=%d%s gm(C/B)=%.4f | contention "
          "A=%.3f B=%.3f | C acted=%llu calls=%llu | residualDelta=%+lld\n",
          std::string(mv::family_name(family)).c_str(),
          mv::pool_target_size(48, factor), cell.seeds, cell.excluded,
          uninterpretable ? " UNINTERPRETABLE" : "", gm,
          cell.ticks_a ? static_cast<double>(cell.contended_a) / cell.ticks_a
                       : 0.0,
          cell.ticks_b ? static_cast<double>(cell.contended_b) / cell.ticks_b
                       : 0.0,
          static_cast<unsigned long long>(cell.acting_c),
          static_cast<unsigned long long>(cell.calls_c),
          cell.residual_delta);
      for (const auto v : cell.log_ratios) {
        pooled_logs.push_back(v);
        per_factor_logs[factor].push_back(v);
        if (!uninterpretable) pooled_logs_strict.push_back(v);
      }
      for (const auto v : cell.log_ratios_ab) pooled_logs_ab.push_back(v);
    }
  }

  std::printf("\n");
  for (std::size_t factor = 0; factor < 3; ++factor) {
    if (per_factor_logs[factor].empty()) continue;
    const auto gm = geo_mean(per_factor_logs[factor]);
    const auto [lo, hi] = bootstrap_ci(per_factor_logs[factor]);
    std::printf("factor M=%zu: gm(C/B)=%.4f CI [%.4f, %.4f] over %zu seeds\n",
                mv::pool_target_size(48, factor), gm, lo, hi,
                per_factor_logs[factor].size());
  }
  {
    // The dispatch-quality statistic the pre-registration asks to report:
    // how much of the anonymous-pool advantage a caller gets from
    // dispatch assignment alone (greedy vs optimal), no reassignment.
    const auto gm_ab = geo_mean(pooled_logs_ab);
    const auto [lo_ab, hi_ab] = bootstrap_ci(pooled_logs_ab);
    std::printf(
        "dispatch quality gm(A/B)=%.4f CI [%.4f, %.4f] seeds=%zu of %d "
        "(rest excluded by the severity rule)\n",
        gm_ab, lo_ab, hi_ab, pooled_logs_ab.size(), total_seeds);
  }
  {
    // Sensitivity: the pooled statistic minus every seed from a cell over
    // the 20% exclusion cap, so the verdict cannot hinge on including an
    // uninterpretable cell's surviving seeds.
    const auto gm_strict = geo_mean(pooled_logs_strict);
    std::printf("pooled excluding uninterpretable cells: gm=%.4f over %zu\n",
                gm_strict, pooled_logs_strict.size());
  }
  const auto gm = geo_mean(pooled_logs);
  const auto [lo, hi] = bootstrap_ci(pooled_logs);
  std::printf(
      "POOLED gm(C/B)=%.4f CI [%.4f, %.4f] seeds=%zu excluded=%d/%d "
      "control-B contention=%.4f\n",
      gm, lo, hi, pooled_logs.size(), total_excluded, total_seeds,
      total_ticks_b
          ? static_cast<double>(total_contended_b) / total_ticks_b
          : 0.0);
  const bool accept = gm <= 0.92 && hi < 1.0;
  std::printf("pre-registered bar: gm<=0.92 AND CI excludes 1.0 -> %s%s\n",
              accept ? "ACCEPT" : "REJECT",
              any_family_uninterpretable
                  ? " (>=1 family cell over the 20% exclusion cap)"
                  : "");
  std::printf("%s\n", failures == 0 ? "ALL GATES PASSED" : "GATE FAILURES");
  return failures == 0 ? 0 : 2;
}
```
