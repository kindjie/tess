// PR C4 Phase A (issue #253): conflict-local temporal escalation gates.
// The plain production tier's C3 verdicts stay pinned FAILING in
// tess_reciprocal_conflict_test.cc -- Phase A changes no library code,
// so those pins still describe the tier. THIS file pins what the
// escalation harness adds on top: the three failing conflicts resolve
// within C3's own pre-registered bound, the three passes are untouched
// (escalation never fires on them), queued-yields resolves without its
// dynamically formed seal, the mechanism is strictly inert on every
// clean C0 seed -- and the per-agent non-worsening gate on residual
// seeds FAILED (2 of 61 seeds worsen one agent each), which closes
// Phase A as attempted-with-partial-success and keeps Phase B
// unproposed. See the substrate test for the pinned record.
#include <gtest/gtest.h>

#include <cstdint>
#include <span>
#include <vector>

#include "escalation_harness.h"
#include "movement_scenarios.h"
#include "reciprocal_fixtures.h"

namespace {

namespace mv = tess_test::movement;
using namespace tess_test::movement::reciprocal;  // NOLINT

[[nodiscard]] std::uint64_t outcome_digest(const mv::Scenario& scenario,
                                           const mv::Outcome& outcome) {
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

struct ArmedRun {
  mv::Outcome outcome;
  mv::EscalationStats stats;
  std::uint64_t digest = 0;
};

[[nodiscard]] ArmedRun run_armed(const Fixture& fixture) {
  auto scenario = build_fixture_scenario(fixture);
  const auto ranking = mv::route_attachment_ranking(*scenario);
  ArmedRun run;
  run.outcome = mv::settle_with_pibt_escalation(*scenario, ranking, &run.stats);
  run.digest = outcome_digest(*scenario, run.outcome);
  return run;
}

// ---------------------------------------------------------------------
// Component extraction unit tests: hand-built cases, because a generated
// instance cannot prove which agents belong in a component.
// ---------------------------------------------------------------------

TEST(Escalation, ComponentClosesOverTouchingAgentsAndBounds) {
  // Three agents in a row on open terrain: seed the middle one; both
  // neighbors stand inside its radius-2 region, so the closure must
  // absorb them; a distant fourth agent must stay out.
  auto scenario = std::make_unique<mv::Scenario>();
  scenario->options.extent = 16;
  scenario->terrain.assign(std::size_t{16} * 16, 1u);
  const auto add_agent = [&](int x, int y, int gx, int gy) {
    tess::PathAgentState agent;
    agent.position = tess::Coord3{x, y, 0};
    tess::set_path_agent_goal(scenario->state, agent, tess::Coord3{gx, gy, 0});
    scenario->agents.push_back(agent);
  };
  add_agent(4, 4, 8, 4);
  add_agent(5, 4, 1, 4);
  add_agent(6, 4, 2, 4);
  add_agent(12, 12, 3, 12);
  const auto component =
      mv::extract_component(*scenario, {1}, mv::EscalationParams{});
  ASSERT_EQ(component.agents.size(), 3u);
  EXPECT_EQ(component.agents[0], 0u);
  EXPECT_EQ(component.agents[1], 1u);
  EXPECT_EQ(component.agents[2], 2u);
  EXPECT_FALSE(component.over_bounds);
  EXPECT_FALSE(component.region.contains(tess::Coord3{12, 12, 0}));
}

TEST(Escalation, OverBoundsComponentIsSkippedNotTruncated) {
  // Seven agents chained across open terrain exceed A_max = 6; the
  // pre-registered outcome is a skip, never a truncated component.
  auto scenario = std::make_unique<mv::Scenario>();
  scenario->options.extent = 16;
  scenario->terrain.assign(std::size_t{16} * 16, 1u);
  for (int i = 0; i < 7; ++i) {
    tess::PathAgentState agent;
    agent.position = tess::Coord3{2 + i, 8, 0};
    tess::set_path_agent_goal(scenario->state, agent, tess::Coord3{14, 8, 0});
    scenario->agents.push_back(agent);
  }
  const auto component =
      mv::extract_component(*scenario, {0}, mv::EscalationParams{});
  EXPECT_EQ(component.agents.size(), 7u);
  EXPECT_TRUE(component.over_bounds);
}

// ---------------------------------------------------------------------
// The acceptance gates.
// ---------------------------------------------------------------------

void expect_resolved_within_bound(const Fixture& fixture) {
  const auto optimal = oracle_optimal_makespan(fixture);
  ASSERT_NE(optimal, kOracleUnsolved) << fixture.name;
  const auto run = run_armed(fixture);
  const auto replay = run_armed(fixture);
  EXPECT_EQ(run.digest, replay.digest) << fixture.name << ": replay";
  EXPECT_GE(run.stats.fired, 1u) << fixture.name;
  EXPECT_TRUE(within_bound(run.outcome, optimal))
      << fixture.name << ": ticks=" << run.outcome.ticks
      << " arrived=" << run.outcome.count(mv::Category::Arrived)
      << " fired=" << run.stats.fired << " skipped=" << run.stats.skipped_bounds
      << "/" << run.stats.skipped_unsolvable
      << " aborted=" << run.stats.aborted;
  std::printf(
      "C4 %-22s optimal=%d ticks=%d fired=%llu steps=%llu states=%llu "
      "aborted=%llu\n",
      fixture.name, optimal, run.outcome.ticks,
      static_cast<unsigned long long>(run.stats.fired),
      static_cast<unsigned long long>(run.stats.plan_steps_executed),
      static_cast<unsigned long long>(run.stats.solver_states),
      static_cast<unsigned long long>(run.stats.aborted));
}

TEST(Escalation, ResolvesPocketYieldWithinTheC3Bound) {
  expect_resolved_within_bound(kPocketYield);
}

TEST(Escalation, ResolvesJunctionCrossWithinTheC3Bound) {
  expect_resolved_within_bound(kJunctionCross);
}

TEST(Escalation, ResolvesQueuedYieldsWithoutTheSelfSeal) {
  const auto optimal = oracle_optimal_makespan(kQueuedYields);
  ASSERT_NE(optimal, kOracleUnsolved);
  const auto run = run_armed(kQueuedYields);
  const auto replay = run_armed(kQueuedYields);
  EXPECT_EQ(run.digest, replay.digest);
  EXPECT_GE(run.stats.fired, 1u);
  EXPECT_TRUE(within_bound(run.outcome, optimal))
      << "ticks=" << run.outcome.ticks
      << " arrived=" << run.outcome.count(mv::Category::Arrived);
  // The C3 exhibit: the plain tier arrives half the population and
  // walls the corridor. The escalated run must not form that seal.
  EXPECT_EQ(run.outcome.count(mv::Category::Sealed), 0u);
  EXPECT_EQ(run.outcome.count(mv::Category::Arrived),
            run.outcome.categories.size());
  std::printf("C4 %-22s optimal=%d ticks=%d fired=%llu\n", kQueuedYields.name,
              optimal, run.outcome.ticks,
              static_cast<unsigned long long>(run.stats.fired));
}

TEST(Escalation, NeverFiresOnTheFixturesThePlainTierSolves) {
  // The three C3 passes settle in fewer ticks than the trigger, so the
  // armed harness must produce identical outcomes with zero fires.
  for (const auto* fixture :
       {&kHeadOnSwap, &kRotationForbid, &kRotationPermit}) {
    const auto run = run_armed(*fixture);
    EXPECT_EQ(run.stats.fired, 0u) << fixture->name;
    auto plain_scenario = build_fixture_scenario(*fixture);
    const auto ranking = mv::route_attachment_ranking(*plain_scenario);
    const auto plain = mv::settle_with_pibt(*plain_scenario, ranking);
    EXPECT_EQ(run.outcome.ticks, plain.ticks) << fixture->name;
    EXPECT_EQ(run.digest, outcome_digest(*plain_scenario, plain))
        << fixture->name;
  }
}

struct SubstrateDelta {
  unsigned trial;
  long long arrived;
  long long wedged;
  long long sealed;
};

// The amendment-1 substrate gate FAILED and its measured outcome is
// pinned here rather than the gate quietly weakened: on 2 of 61
// residual seeds an agent ends strictly worse (warehouse trial 10, one
// arrived agent ends sealed; colony trial 10, one wedged agent ends
// sealed), because even a locally-sound intervention perturbs the
// global trajectory. The aggregate strictly improves (+3 arrived, -2
// wedged, -1 sealed across the full 132-seed sweep, captured in the
// evidence directory) and every clean seed is strictly inert -- but per
// the pre-registration's stop condition, the failed gate closes Phase A
// as attempted-with-partial-success: always-on arming is NOT accepted
// and Phase B (public promotion) is not proposed. A future candidate
// that fixes the divergence flips these pins with its own evidence.
//
// Each family checks trials 0 and 1 plus its divergent seeds, split
// per family to honor the 60-second per-test contract; the full sweep
// is evidence, reproducible from the recorded program.
void check_substrate_family(mv::Family family,
                            std::span<const SubstrateDelta> pinned) {
  const auto in_subset = [&](unsigned trial) {
    if (trial <= 1) return true;
    for (const auto& d : pinned) {
      if (d.trial == trial) return true;
    }
    return false;
  };
  for (unsigned trial = 0; trial < mv::trial_count(family); ++trial) {
    if (!in_subset(trial)) continue;
    auto plain_scenario = mv::build_scenario(family, trial);
    const auto plain_ranking = mv::route_attachment_ranking(*plain_scenario);
    const auto plain = mv::settle_with_pibt(*plain_scenario, plain_ranking);

    auto armed_scenario = mv::build_scenario(family, trial);
    const auto armed_ranking = mv::route_attachment_ranking(*armed_scenario);
    mv::EscalationStats stats;
    const auto armed =
        mv::settle_with_pibt_escalation(*armed_scenario, armed_ranking, &stats);

    auto replay_scenario = mv::build_scenario(family, trial);
    const auto replay_ranking = mv::route_attachment_ranking(*replay_scenario);
    mv::EscalationStats replay_stats;
    const auto replay = mv::settle_with_pibt_escalation(
        *replay_scenario, replay_ranking, &replay_stats);
    ASSERT_EQ(outcome_digest(*armed_scenario, armed),
              outcome_digest(*replay_scenario, replay))
        << mv::family_name(family) << " trial " << trial << ": replay";

    const auto plain_clean =
        plain.count(mv::Category::Arrived) == plain.categories.size();
    if (plain_clean) {
      ASSERT_EQ(outcome_digest(*plain_scenario, plain),
                outcome_digest(*armed_scenario, armed))
          << mv::family_name(family) << " trial " << trial;
      ASSERT_EQ(stats.fired, 0u)
          << mv::family_name(family) << " trial " << trial;
      continue;
    }
    const SubstrateDelta observed{
        trial,
        static_cast<long long>(armed.count(mv::Category::Arrived)) -
            static_cast<long long>(plain.count(mv::Category::Arrived)),
        static_cast<long long>(armed.count(mv::Category::Wedged)) -
            static_cast<long long>(plain.count(mv::Category::Wedged)),
        static_cast<long long>(armed.count(mv::Category::Sealed)) -
            static_cast<long long>(plain.count(mv::Category::Sealed))};
    const auto entry =
        std::find_if(pinned.begin(), pinned.end(),
                     [&](const SubstrateDelta& d) { return d.trial == trial; });
    if (entry == pinned.end()) {
      EXPECT_TRUE(observed.arrived == 0 && observed.wedged == 0 &&
                  observed.sealed == 0)
          << mv::family_name(family) << " trial " << trial
          << ": unpinned divergence dArr=" << observed.arrived
          << " dWed=" << observed.wedged << " dSeal=" << observed.sealed;
    } else {
      EXPECT_EQ(observed.arrived, entry->arrived)
          << mv::family_name(family) << " trial " << trial;
      EXPECT_EQ(observed.wedged, entry->wedged)
          << mv::family_name(family) << " trial " << trial;
      EXPECT_EQ(observed.sealed, entry->sealed)
          << mv::family_name(family) << " trial " << trial;
    }
  }
}

TEST(EscalationSubstrate, Warehouse) {
  const SubstrateDelta pinned[] = {{4, +1, 0, -1}, {10, -1, 0, +1}};
  check_substrate_family(mv::Family::Warehouse, pinned);
}
TEST(EscalationSubstrate, Ring) {
  check_substrate_family(mv::Family::Ring, {});
}
TEST(EscalationSubstrate, Colony) {
  const SubstrateDelta pinned[] = {{10, +1, -2, +1}};
  check_substrate_family(mv::Family::Colony, pinned);
}
TEST(EscalationSubstrate, RandomSparse) {
  check_substrate_family(mv::Family::RandomSparse, {});
}
TEST(EscalationSubstrate, RandomMedium) {
  check_substrate_family(mv::Family::RandomMedium, {});
}
TEST(EscalationSubstrate, RandomDense) {
  check_substrate_family(mv::Family::RandomDense, {});
}
TEST(EscalationSubstrate, Adversarial) {
  const SubstrateDelta pinned[] = {{0, +1, 0, -1}, {9, +1, 0, -1}};
  check_substrate_family(mv::Family::Adversarial, pinned);
}

}  // namespace
