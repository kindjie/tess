// PR C3 (issue #247): reciprocal-conflict fixtures and current-tier
// screen. Five hand-built fixtures, each declaring its own SwapPolicy,
// an exhaustive joint-space BFS oracle for the exact optimal makespan,
// and both current tiers run under the C0 settle loop. The
// pre-registered bound: the production PIBT tier passes a fixture iff
// every agent arrives at the fixpoint AND ticks <= max(3 * optimal,
// optimal + 8); a PIBT failure is what would open PR C4. The
// joint-movement tier is context: it has no swap capability at all, so
// swap-only fixtures are structurally beyond it, which is recorded
// rather than counted as a C4 trigger.
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <string>
#include <unordered_set>
#include <vector>

#include "movement_scenarios.h"
#include "reciprocal_fixtures.h"

namespace {

namespace mv = tess_test::movement;
using namespace tess_test::movement::reciprocal;  // NOLINT

// The pre-PIBT reactive stack: canonical routing plus validated movement
// commits. Same settle shape as mv::settle_with_pibt -- no-progress
// fixpoint, cap as safety bound, settle-refresh with route invalidation
// -- so outcomes classify identically. This tier has no JointMoveOptions
// and therefore no swap capability regardless of the fixture's policy.
[[nodiscard]] auto settle_with_movement(mv::Scenario& scenario) -> mv::Outcome {
  mv::Outcome outcome;
  tess::PathRequestRuntime runtime;
  runtime.reserve_requests(128);
  runtime.reserve_search_nodes(16384);
  runtime.reserve_path_nodes(65536);
  auto options = tess::PathAgentTickOptions{};
  options.max_blocked_retries = scenario.options.max_blocked_retries;
  options.blocked_exhaustion_policy = scenario.options.exhaustion_policy;

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
    if (mv::refresh_settled(scenario)) {
      tess::mark_pathing_dirty(scenario.state);
    }
    (void)tess::tick_weighted_path_agents_with_movement<
        mv::World, mv::Traveler, 4u, mv::OccupancyTag, mv::ReservationTag>(
        scenario.state, scenario.world,
        std::span<tess::PathAgentState>(scenario.agents), runtime, options);
    std::vector<tess::Coord3> current;
    current.reserve(scenario.agents.size());
    for (const auto& agent : scenario.agents) {
      current.push_back(agent.position);
    }
    stalled = (current == previous) ? stalled + 1 : 0;
    previous = std::move(current);
    if (stalled >= scenario.options.wedge_ticks) {
      outcome.fixpoint = true;
      ++tick;
      break;
    }
  }
  outcome.ticks = tick;
  outcome.censored = !outcome.fixpoint;
  outcome.categories = mv::classify(scenario, outcome.censored);
  outcome.structural_seals = mv::structural_seal_count(scenario);
  return outcome;
}

// The plan's joint-movement configuration: canonical routing with the
// JOINT admission (cycles longer than two commit; SwapPolicy honored).
// Same settle shape again.
[[nodiscard]] auto settle_with_joint_movement(mv::Scenario& scenario)
    -> mv::Outcome {
  mv::Outcome outcome;
  tess::PathRequestRuntime runtime;
  runtime.reserve_requests(128);
  runtime.reserve_search_nodes(16384);
  runtime.reserve_path_nodes(65536);
  tess::JointMoveScratch scratch;
  scratch.reserve(scenario.agents.size());
  auto options = tess::PathAgentTickOptions{};
  options.max_blocked_retries = scenario.options.max_blocked_retries;
  options.blocked_exhaustion_policy = scenario.options.exhaustion_policy;
  const auto move_options = tess::JointMoveOptions{scenario.options.swap};

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
    if (mv::refresh_settled(scenario)) {
      tess::mark_pathing_dirty(scenario.state);
    }
    tess::JointMoveStats move_stats;
    (void)tess::tick_weighted_path_agents_with_joint_movement<
        mv::World, mv::Traveler, 4u, mv::OccupancyTag, mv::ReservationTag>(
        scenario.state, scenario.world,
        std::span<tess::PathAgentState>(scenario.agents), runtime, scratch,
        options, move_options, nullptr, &move_stats);
    outcome.swaps += move_stats.swaps;
    outcome.swaps_denied += move_stats.swaps_denied;
    std::vector<tess::Coord3> current;
    current.reserve(scenario.agents.size());
    for (const auto& agent : scenario.agents) {
      current.push_back(agent.position);
    }
    stalled = (current == previous) ? stalled + 1 : 0;
    previous = std::move(current);
    if (stalled >= scenario.options.wedge_ticks) {
      outcome.fixpoint = true;
      ++tick;
      break;
    }
  }
  outcome.ticks = tick;
  outcome.censored = !outcome.fixpoint;
  outcome.categories = mv::classify(scenario, outcome.censored);
  outcome.structural_seals = mv::structural_seal_count(scenario);
  return outcome;
}

struct TierResult {
  mv::Outcome outcome;
  std::uint64_t digest = 0;
};

[[nodiscard]] auto outcome_digest(const mv::Scenario& scenario,
                                  const mv::Outcome& outcome) -> std::uint64_t {
  std::uint64_t hash = 0xCBF29CE484222325ULL;
  const auto mix = [&hash](std::uint64_t value) {
    hash ^= value + 0x9E3779B97F4A7C15ULL + (hash << 6U) + (hash >> 2U);
  };
  for (const auto& agent : scenario.agents) {
    mix(static_cast<std::uint64_t>(agent.position.x));
    mix(static_cast<std::uint64_t>(agent.position.y));
  }
  for (const auto category : outcome.categories) {
    mix(static_cast<std::uint64_t>(category));
  }
  mix(static_cast<std::uint64_t>(outcome.ticks));
  return hash;
}

[[nodiscard]] TierResult run_pibt(const Fixture& fixture) {
  auto scenario = build_fixture_scenario(fixture);
  const auto ranking = mv::route_attachment_ranking(*scenario);
  TierResult result{mv::settle_with_pibt(*scenario, ranking), 0};
  result.digest = outcome_digest(*scenario, result.outcome);
  return result;
}

[[nodiscard]] TierResult run_movement(const Fixture& fixture) {
  auto scenario = build_fixture_scenario(fixture);
  TierResult result{settle_with_movement(*scenario), 0};
  result.digest = outcome_digest(*scenario, result.outcome);
  return result;
}

[[nodiscard]] TierResult run_joint(const Fixture& fixture) {
  auto scenario = build_fixture_scenario(fixture);
  TierResult result{settle_with_joint_movement(*scenario), 0};
  result.digest = outcome_digest(*scenario, result.outcome);
  return result;
}

void report(const char* tier, const Fixture& fixture, int optimal,
            const mv::Outcome& outcome) {
  std::printf(
      "C3 %-22s %-9s optimal=%d bound=%d ticks=%d arrived=%zu wedged=%zu "
      "sealed=%zu goal_occupied=%zu censored=%zu -> %s\n",
      fixture.name, tier, optimal, std::max(3 * optimal, optimal + 8),
      outcome.ticks, outcome.count(mv::Category::Arrived),
      outcome.count(mv::Category::Wedged), outcome.count(mv::Category::Sealed),
      outcome.count(mv::Category::GoalOccupied),
      outcome.count(mv::Category::Censored),
      within_bound(outcome, optimal) ? "PASS" : "FAIL");
}

// Diagnostic context, not a decision arm: the same PIBT machinery under
// per-agent exact BFS ranking, the configuration the pinned regression
// in tess_pibt_movement_test.cc proves can pocket-yield. Where the
// production RouteAttachmentRanking fails a fixture and this passes it,
// the failure localizes to the ranking rather than to PIBT itself --
// which is exactly the boundary a C4 candidate needs to know.
[[nodiscard]] TierResult run_pibt_bfs_ranked(const Fixture& fixture) {
  auto scenario = build_fixture_scenario(fixture);
  const auto extent = scenario->options.extent;
  const auto& terrain = scenario->terrain;
  const auto bfs_distance = [&](tess::Coord3 goal, tess::Coord3 from) {
    std::vector<std::uint32_t> dist(
        static_cast<std::size_t>(extent) * static_cast<std::size_t>(extent),
        0xFFFFFFFFu);
    std::deque<tess::Coord3> frontier{goal};
    const auto cell_index = [extent](tess::Coord3 c) {
      return static_cast<std::size_t>(c.y) * static_cast<std::size_t>(extent) +
             static_cast<std::size_t>(c.x);
    };
    dist[cell_index(goal)] = 0;
    while (!frontier.empty()) {
      const auto cur = frontier.front();
      frontier.pop_front();
      const auto d = dist[cell_index(cur)];
      const std::array<tess::Coord3, 4> steps = {
          tess::Coord3{cur.x + 1, cur.y, 0}, tess::Coord3{cur.x - 1, cur.y, 0},
          tess::Coord3{cur.x, cur.y + 1, 0}, tess::Coord3{cur.x, cur.y - 1, 0}};
      for (const auto step : steps) {
        if (step.x < 0 || step.y < 0 || step.x >= extent || step.y >= extent) {
          continue;
        }
        if (!mv::grid_at(terrain, extent, static_cast<int>(step.x),
                         static_cast<int>(step.y))) {
          continue;
        }
        auto& cell = dist[cell_index(step)];
        if (cell != 0xFFFFFFFFu) continue;
        cell = d + 1;
        frontier.push_back(step);
      }
    }
    return dist[cell_index(from)];
  };
  const auto& agents = scenario->agents;
  const auto rank = [&](std::size_t agent, tess::Coord3 c) -> std::uint32_t {
    const auto goal =
        agents[agent].has_goal ? agents[agent].goal : agents[agent].position;
    return bfs_distance(goal, c);
  };
  TierResult result{mv::settle_with_pibt(*scenario, rank), 0};
  result.digest = outcome_digest(*scenario, result.outcome);
  return result;
}

// Shared body: oracle, determinism replays, the pre-registered PIBT
// bound, the diagnostic ranking arm, and the printed evidence rows.
// `expect_*` pin the OBSERVED verdicts (the bound itself was
// pre-registered in #247 before any run): where the production tier is
// pinned failing, that pin is PR C4's acceptance gate, and a candidate
// that resolves the fixture must flip it.
void screen_fixture(const Fixture& fixture, bool expect_pibt_pass,
                    bool expect_joint_pass, bool expect_movement_pass,
                    bool expect_bfs_rank_pass) {
  const auto optimal = oracle_optimal_makespan(fixture);
  ASSERT_NE(optimal, kOracleUnsolved)
      << fixture.name << ": oracle exhausted or fixture unsolvable, which "
      << "the pre-registration declares an invalid fixture";
  // Every arm whose numbers reach the evidence record is replayed and
  // must reproduce bit-identically.
  const auto pibt = run_pibt(fixture);
  const auto pibt_replay = run_pibt(fixture);
  EXPECT_EQ(pibt.digest, pibt_replay.digest) << fixture.name;
  const auto joint = run_joint(fixture);
  const auto joint_replay = run_joint(fixture);
  EXPECT_EQ(joint.digest, joint_replay.digest) << fixture.name;
  const auto movement = run_movement(fixture);
  const auto movement_replay = run_movement(fixture);
  EXPECT_EQ(movement.digest, movement_replay.digest) << fixture.name;
  const auto bfs_ranked = run_pibt_bfs_ranked(fixture);
  const auto bfs_replay = run_pibt_bfs_ranked(fixture);
  EXPECT_EQ(bfs_ranked.digest, bfs_replay.digest) << fixture.name;
  report("pibt", fixture, optimal, pibt.outcome);
  report("joint", fixture, optimal, joint.outcome);
  report("seq", fixture, optimal, movement.outcome);
  report("pibt-bfs", fixture, optimal, bfs_ranked.outcome);
  EXPECT_EQ(within_bound(pibt.outcome, optimal), expect_pibt_pass)
      << fixture.name << ": production tier verdict changed";
  EXPECT_EQ(within_bound(joint.outcome, optimal), expect_joint_pass)
      << fixture.name;
  EXPECT_EQ(within_bound(movement.outcome, optimal), expect_movement_pass)
      << fixture.name;
  EXPECT_EQ(within_bound(bfs_ranked.outcome, optimal), expect_bfs_rank_pass)
      << fixture.name;
}

TEST(ReciprocalConflict, PocketYieldCorridorForbid) {
  screen_fixture(kPocketYield, false, false, false, false);
}

TEST(ReciprocalConflict, HeadOnCorridorPermit) {
  screen_fixture(kHeadOnSwap, true, true, false, true);
}

TEST(ReciprocalConflict, RotationCycleForbid) {
  screen_fixture(kRotationForbid, true, true, false, true);
}

TEST(ReciprocalConflict, RotationCyclePermit) {
  screen_fixture(kRotationPermit, true, true, false, true);
}

TEST(ReciprocalConflict, JunctionCrossForbid) {
  screen_fixture(kJunctionCross, false, false, false, false);
}

TEST(ReciprocalConflict, QueuedYieldsForbid) {
  screen_fixture(kQueuedYields, false, false, false, false);
}

// The six instances are digest-pinned like C0's: hand-built constants
// arguably pin themselves, but the digest catches an accidental edit to
// a map string or an agent spec that a reviewer's eye would miss, and
// C4 will compare against exactly these instances.
struct FixtureDigestEntry {
  const Fixture* fixture = nullptr;
  std::uint64_t digest = 0;
};

const std::array<FixtureDigestEntry, 6> kFixtureDigests = {{
    {&kPocketYield, 12551001356545419851ULL},
    {&kHeadOnSwap, 3975504206594097402ULL},
    {&kRotationForbid, 4894647785238051775ULL},
    {&kRotationPermit, 4894647785238051760ULL},
    {&kJunctionCross, 15372426116064309569ULL},
    {&kQueuedYields, 5393537293545786904ULL},
}};

// scenario_digest covers terrain, starts, and goals but not the declared
// SwapPolicy, which is equally part of the instance (the two rotation
// fixtures differ only there); mix it in.
[[nodiscard]] std::uint64_t fixture_digest(const Fixture& fixture) {
  const auto scenario = build_fixture_scenario(fixture);
  auto hash = mv::scenario_digest(*scenario);
  hash ^= static_cast<std::uint64_t>(fixture.swap) + 0x9E3779B97F4A7C15ULL +
          (hash << 6U) + (hash >> 2U);
  return hash;
}

TEST(ReciprocalConflict, FixtureDigestsMatchTheCommittedTable) {
  // getenv suppression rationale as in tess_movement_scenarios_test.cc.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
  const char* const print_digests = std::getenv("TESS_PRINT_SCENARIO_DIGESTS");
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
  if (print_digests != nullptr) {
    for (const auto& entry : kFixtureDigests) {
      std::printf(
          "    %s -> %lluULL\n", entry.fixture->name,
          static_cast<unsigned long long>(fixture_digest(*entry.fixture)));
    }
    GTEST_SKIP() << "printed fixture digests";
  }
  for (const auto& entry : kFixtureDigests) {
    EXPECT_EQ(fixture_digest(*entry.fixture), entry.digest)
        << entry.fixture->name
        << ": the instance changed; C4 comparisons refer to a different "
           "fixture";
  }
}

// The oracle itself needs cases whose answers are known by hand, or a
// wrong conflict model passes every fixture silently. The 2x2 rotation
// has makespan exactly 1 under both policies. The Permit head-on pair
// takes 5, not the one-way distance 4: A(t) = t meets B(t) = 4 - t at
// t = 2, so any 4-tick schedule collides and one agent must deviate for
// exactly one tick. The same corridor under Forbid is unsolvable.
TEST(ReciprocalConflict, OracleMatchesHandComputedCases) {
  EXPECT_EQ(oracle_optimal_makespan(kRotationForbid), 1);
  EXPECT_EQ(oracle_optimal_makespan(kRotationPermit), 1);
  EXPECT_EQ(oracle_optimal_makespan(kHeadOnSwap), 5);
  Fixture unsolvable = kHeadOnSwap;
  unsolvable.name = "head_on/forbid";
  unsolvable.swap = tess::SwapPolicy::Forbid;
  EXPECT_EQ(oracle_optimal_makespan(unsolvable), kOracleUnsolved);
}

}  // namespace
