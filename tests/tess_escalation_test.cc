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
#include <cstdlib>
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
  // Three agents in a row on passable terrain: seed the middle one; both
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
  // Seven agents chained across passable terrain exceed A_max = 6; the
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

struct SubstratePin {
  mv::Family family;
  unsigned trial;
  // Outcome digests of the plain and armed runs (positions, categories,
  // ticks). Cross-build constants: comparing one armed run against its
  // pinned digest proves determinism more strongly than an in-process
  // replay, and it costs one settle per seed instead of three -- the
  // difference between fitting and blowing the 60-second budget on
  // sanitizer and MSVC-debug runners, where the capped escalation
  // solves are an order of magnitude slower.
  std::uint64_t plain_digest;
  std::uint64_t armed_digest;
  std::uint64_t fires;
};

// The amendment-1 substrate gate FAILED and its measured outcome is
// pinned here rather than the gate quietly weakened: warehouse trial 10
// worsens one arrived agent to sealed and colony trial 10 worsens one
// wedged agent to sealed (against three strictly improved seeds and 56
// unchanged ones), because even a locally-sound intervention perturbs
// the global trajectory. Phase A therefore closes as
// attempted-with-partial-success: always-on arming is NOT accepted and
// Phase B (public promotion) is not proposed. Where plain_digest ==
// armed_digest the seed is strictly inert (fires must be zero); where
// they differ, the armed digest pins the exact measured divergence. A
// future candidate that fixes the divergence flips these pins with its
// own evidence. Regenerate with TESS_PRINT_SCENARIO_DIGESTS=1, only
// when the mechanism deliberately changes.
inline constexpr std::array<SubstratePin, 18> kSubstratePins = {{
    {mv::Family::Warehouse, 0, 12231528207352709562ULL, 12231528207352709562ULL,
     0},
    {mv::Family::Warehouse, 1, 1342190838974649377ULL, 1342190838974649380ULL,
     4},
    {mv::Family::Warehouse, 4, 12262282791876795707ULL, 17119805085449989024ULL,
     1},
    {mv::Family::Warehouse, 10, 15469116792091508126ULL, 2610868724167501643ULL,
     3},
    {mv::Family::Ring, 0, 2788241593598551195ULL, 2788241593598551195ULL, 0},
    {mv::Family::Ring, 1, 16904403175910491626ULL, 16904403175910491626ULL, 0},
    {mv::Family::Colony, 0, 10542000121274296210ULL, 10542000121274296210ULL,
     0},
    {mv::Family::Colony, 1, 14677821556300876558ULL, 14677821556300876558ULL,
     0},
    {mv::Family::Colony, 10, 3139476430789227227ULL, 7627358141029263507ULL, 1},
    {mv::Family::RandomSparse, 0, 9534388355605995680ULL,
     9534388355605995680ULL, 0},
    {mv::Family::RandomSparse, 1, 1328679313291105906ULL,
     1328679313291105906ULL, 0},
    {mv::Family::RandomMedium, 0, 370501214086394417ULL, 370501214086394417ULL,
     0},
    {mv::Family::RandomMedium, 1, 3869611643733695240ULL,
     3869611643733695240ULL, 0},
    {mv::Family::RandomDense, 0, 14825455329457929772ULL,
     14825455329457929772ULL, 0},
    {mv::Family::RandomDense, 1, 1293045498550025900ULL, 1293045498550025900ULL,
     0},
    {mv::Family::Adversarial, 0, 12168179487580968109ULL,
     17349866327757600270ULL, 1},
    {mv::Family::Adversarial, 1, 7450950821103527188ULL, 7450950821103527188ULL,
     0},
    {mv::Family::Adversarial, 9, 10698998787488398025ULL,
     14553739374324806754ULL, 1},
}};

// Sanitizer and MSVC-debug runners execute the capped escalation
// solves an order of magnitude slower; the single heaviest pinned seed
// carries a config guard so those runners skip it inside the 60-second
// contract. Every optimized configuration (dev, dev-werror, GCC,
// libc++, release) still asserts it, and the full sweep is evidence.
#ifndef TESS_TEST_SLOW_CONFIG
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define TESS_TEST_SLOW_CONFIG 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
#define TESS_TEST_SLOW_CONFIG 1
#endif
#endif
#if !defined(TESS_TEST_SLOW_CONFIG) && defined(_MSC_VER) && !defined(NDEBUG)
#define TESS_TEST_SLOW_CONFIG 1
#endif
#ifndef TESS_TEST_SLOW_CONFIG
#define TESS_TEST_SLOW_CONFIG 0
#endif
#endif

void check_substrate_pin(const SubstratePin& pin) {
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
  const char* const print_digests = std::getenv("TESS_PRINT_SCENARIO_DIGESTS");
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
  if (print_digests != nullptr) {
    auto plain_scenario = mv::build_scenario(pin.family, pin.trial);
    const auto plain_ranking = mv::route_attachment_ranking(*plain_scenario);
    const auto plain = mv::settle_with_pibt(*plain_scenario, plain_ranking);
    auto armed_scenario = mv::build_scenario(pin.family, pin.trial);
    const auto armed_ranking = mv::route_attachment_ranking(*armed_scenario);
    mv::EscalationStats stats;
    const auto armed =
        mv::settle_with_pibt_escalation(*armed_scenario, armed_ranking, &stats);
    std::printf(
        "    {mv::Family::%s, %u, %lluULL, %lluULL, %llu},\n",
        std::string(mv::family_name(pin.family)).c_str(), pin.trial,
        static_cast<unsigned long long>(outcome_digest(*plain_scenario, plain)),
        static_cast<unsigned long long>(outcome_digest(*armed_scenario, armed)),
        static_cast<unsigned long long>(stats.fired));
    GTEST_SKIP() << "printed substrate pins";
    return;
  }
  auto armed_scenario = mv::build_scenario(pin.family, pin.trial);
  const auto armed_ranking = mv::route_attachment_ranking(*armed_scenario);
  mv::EscalationStats stats;
  const auto armed =
      mv::settle_with_pibt_escalation(*armed_scenario, armed_ranking, &stats);
  EXPECT_EQ(outcome_digest(*armed_scenario, armed), pin.armed_digest)
      << mv::family_name(pin.family) << " trial " << pin.trial;
  EXPECT_EQ(stats.fired, pin.fires)
      << mv::family_name(pin.family) << " trial " << pin.trial;
  if (pin.plain_digest == pin.armed_digest) {
    EXPECT_EQ(stats.fired, 0u) << mv::family_name(pin.family) << " trial "
                               << pin.trial << ": an inert seed must not fire";
  }
}

#define TESS_SUBSTRATE_TEST(name, index)        \
  TEST(EscalationSubstrate, name) {             \
    check_substrate_pin(kSubstratePins[index]); \
  }

TESS_SUBSTRATE_TEST(WarehouseT0, 0)
TESS_SUBSTRATE_TEST(WarehouseT1, 1)
TESS_SUBSTRATE_TEST(WarehouseT4Improves, 2)
TESS_SUBSTRATE_TEST(WarehouseT10Worsens, 3)
TESS_SUBSTRATE_TEST(RingT0, 4)
TESS_SUBSTRATE_TEST(RingT1, 5)
TESS_SUBSTRATE_TEST(ColonyT0, 6)
TESS_SUBSTRATE_TEST(ColonyT1, 7)
TESS_SUBSTRATE_TEST(ColonyT10Mixed, 8)
TESS_SUBSTRATE_TEST(RandomSparseT0, 9)
TESS_SUBSTRATE_TEST(RandomSparseT1, 10)
TESS_SUBSTRATE_TEST(RandomMediumT0, 11)
TESS_SUBSTRATE_TEST(RandomMediumT1, 12)
TESS_SUBSTRATE_TEST(RandomDenseT0, 13)
TESS_SUBSTRATE_TEST(RandomDenseT1, 14)
TEST(EscalationSubstrate, AdversarialT0Improves) {
#if TESS_TEST_SLOW_CONFIG
  GTEST_SKIP() << "capped-solve seed skipped on sanitizer/MSVC-debug "
                  "runners; asserted on every optimized configuration";
#else
  check_substrate_pin(kSubstratePins[15]);
#endif
}
TESS_SUBSTRATE_TEST(AdversarialT1, 16)
TESS_SUBSTRATE_TEST(AdversarialT9Improves, 17)

}  // namespace
