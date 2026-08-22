#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "movement_scenarios.h"

namespace {

namespace mv = tess_test::movement;

constexpr std::array<mv::Family, 7> kFamilies = {
    mv::Family::Warehouse,    mv::Family::Ring,         mv::Family::Colony,
    mv::Family::RandomSparse, mv::Family::RandomMedium, mv::Family::RandomDense,
    mv::Family::Adversarial,
};

// Committed instance digests. These pin terrain, starts, and goals for
// trial 0 of every family across build configurations, which a single
// test binary cannot check by comparing itself. They also protect the
// ring extraction: the original construction consumed its RNG stream in
// a fixed order, so a silent reorder would change the instance under an
// unchanged seed and every later comparison would quietly shift.
//
// Regenerate deliberately, never to make a red test pass: a changed
// digest means every previously recorded result for that family refers
// to a different instance.
struct DigestEntry {
  mv::Family family = mv::Family::Ring;
  unsigned trial = 0;
  std::uint64_t digest = 0;
};

// Set TESS_PRINT_SCENARIO_DIGESTS=1 to regenerate this table. Do that
// only when the instances are meant to change; a changed digest means
// every result previously recorded against that family describes a
// different instance.
constexpr std::array<DigestEntry, 7> kDigests = {{
    {mv::Family::Warehouse, 0, 2464618294147250866ULL},
    {mv::Family::Ring, 0, 1747672920075825676ULL},
    {mv::Family::Colony, 0, 14139668748370239316ULL},
    {mv::Family::RandomSparse, 0, 15387204627193774012ULL},
    {mv::Family::RandomMedium, 0, 6054802299000204286ULL},
    {mv::Family::RandomDense, 0, 865693069020214646ULL},
    {mv::Family::Adversarial, 0, 11353542124941068605ULL},
}};

TEST(MovementScenarios, InstanceDigestsMatchTheCommittedTable) {
  // MSVC deprecates getenv because the returned pointer aliases a static
  // buffer a concurrent setenv could invalidate. This reads the variable
  // once at test start and only compares it against null, so the hazard
  // does not apply. Same suppression pattern as property_harness.h.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
  const char* const print_digests = std::getenv("TESS_PRINT_SCENARIO_DIGESTS");
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
  if (print_digests != nullptr) {
    for (const auto& entry : kDigests) {
      const auto scenario = mv::build_scenario(entry.family, entry.trial);
      std::printf(
          "    %s trial %u -> %lluULL\n",
          std::string(mv::family_name(entry.family)).c_str(), entry.trial,
          static_cast<unsigned long long>(mv::scenario_digest(*scenario)));
    }
    GTEST_SKIP() << "printed digests";
  }
  for (const auto& entry : kDigests) {
    const auto scenario = mv::build_scenario(entry.family, entry.trial);
    ASSERT_NE(scenario, nullptr);
    EXPECT_EQ(mv::scenario_digest(*scenario), entry.digest)
        << mv::family_name(entry.family) << " trial " << entry.trial
        << ": the instance changed, so earlier results for this family "
           "describe a different fixture";
  }
}

TEST(MovementScenarios, SeedsAreAClosedFormulaWithoutPerSeedFreedom) {
  // The seed schedule must be reproducible from the family and trial
  // index alone. If a curated list ever replaces the formula, this test
  // is the thing that should stop it: a list would let a seed be
  // dropped after its result was seen.
  // Recomputed independently of the header rather than comparing the
  // function to itself, which any implementation -- including a curated
  // lookup table -- would satisfy.
  const auto expected = [](mv::Family family, unsigned trial) {
    std::uint64_t z =
        0x9E3779B97F4A7C15ULL * (static_cast<std::uint64_t>(family) + 1ULL) *
            1000003ULL +
        0x9E3779B97F4A7C15ULL * (static_cast<std::uint64_t>(trial) + 1ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
  };
  for (const auto family : kFamilies) {
    for (unsigned trial = 0; trial < mv::trial_count(family); ++trial) {
      EXPECT_EQ(mv::scenario_seed(family, trial), expected(family, trial))
          << mv::family_name(family) << " trial " << trial;
    }
  }
  // Distinct families and trials must not collide, or two "independent"
  // trials would be one instance counted twice.
  std::vector<std::uint64_t> seeds;
  for (const auto family : kFamilies) {
    for (unsigned trial = 0; trial < mv::trial_count(family); ++trial) {
      seeds.push_back(mv::scenario_seed(family, trial));
    }
  }
  std::sort(seeds.begin(), seeds.end());
  EXPECT_EQ(std::adjacent_find(seeds.begin(), seeds.end()), seeds.end())
      << "two trials share a seed";
}

TEST(MovementScenarios, EveryFamilyBuildsAPopulatedInstance) {
  for (const auto family : kFamilies) {
    const auto scenario = mv::build_scenario(family, 0);
    ASSERT_NE(scenario, nullptr) << mv::family_name(family);
    EXPECT_FALSE(scenario->agents.empty()) << mv::family_name(family);
    // Every agent must start somewhere its own class can stand, or the
    // run measures a setup defect rather than a movement policy.
    for (const auto& agent : scenario->agents) {
      EXPECT_TRUE(mv::grid_at(scenario->terrain, scenario->options.extent,
                              static_cast<int>(agent.position.x),
                              static_cast<int>(agent.position.y)))
          << mv::family_name(family);
    }
  }
}

TEST(MovementScenarios, StructurallyUnreachableGoalsAreCountedNotHidden) {
  // Some random-fill instances strand a goal behind solid terrain before
  // any agent moves. Those read as sealed although no seal formed, so
  // the count is reported separately rather than credited to a mover.
  // The families whose terrain is connected by construction must report
  // none, which is what makes the counter meaningful where it is not
  // zero.
  for (const auto family :
       {mv::Family::Warehouse, mv::Family::Ring, mv::Family::Adversarial}) {
    const auto scenario = mv::build_scenario(family, 0);
    EXPECT_EQ(mv::structural_seal_count(*scenario), 0u)
        << mv::family_name(family) << " terrain should be connected";
  }
  // And the counter must be able to see one when it exists.
  auto walled = std::make_unique<mv::Scenario>();
  walled->options.extent = 16;
  walled->terrain.assign(std::size_t{16} * 16, 0u);
  mv::grid_set(walled->terrain, 16, 2, 8, true);
  mv::grid_set(walled->terrain, 16, 13, 3, true);
  tess::PathAgentState mover;
  mover.position = tess::Coord3{2, 8, 0};
  mover.goal = tess::Coord3{13, 3, 0};
  mover.has_goal = true;
  mover.phase = tess::PathAgentPhase::Following;
  walled->agents.push_back(mover);
  EXPECT_EQ(mv::structural_seal_count(*walled), 1u);
}

TEST(MovementScenarios, RebuildingAScenarioReproducesItExactly) {
  // Bit-identical rebuild is the cheapest guard against a generator
  // reading anything but its seed.
  for (const auto family : kFamilies) {
    for (unsigned trial = 0; trial < 3; ++trial) {
      const auto first = mv::build_scenario(family, trial);
      const auto second = mv::build_scenario(family, trial);
      ASSERT_NE(first, nullptr);
      ASSERT_NE(second, nullptr);
      EXPECT_EQ(mv::scenario_digest(*first), mv::scenario_digest(*second))
          << mv::family_name(family) << " trial " << trial;
    }
  }
}

TEST(MovementScenarios, TwoConsecutiveRunsOfOneSeedAreIdentical) {
  // This is the test that catches priority state leaking between runs.
  // `PibtPriorities` is index-paired with the agent span and only ever
  // grows, so a harness that reused one across runs would carry stale
  // `elapsed` into the next and silently change decision order. The
  // settle loop constructs it fresh; this proves it.
  const auto run = [](mv::Family family, unsigned trial) {
    auto scenario = mv::build_scenario(family, trial);
    auto rank = mv::route_attachment_ranking(*scenario);
    return mv::settle_with_pibt(*scenario, rank);
  };
  for (const auto family :
       {mv::Family::Warehouse, mv::Family::Ring, mv::Family::Adversarial}) {
    const auto first = run(family, 0);
    const auto second = run(family, 0);
    EXPECT_EQ(first.ticks, second.ticks) << mv::family_name(family);
    EXPECT_EQ(first.categories, second.categories) << mv::family_name(family);
    EXPECT_EQ(first.swaps, second.swaps) << mv::family_name(family);
  }
}

TEST(MovementScenarios, ClassifierSeparatesSealedFromWedged) {
  // Hand-built configurations, because the distinction is the whole
  // reason this harness exists and a generated instance cannot prove
  // which category it should land in.
  //
  // Sealed: a terminal agent stands in the only corridor tile, so the
  // live agent's goal is unreachable for any mover.
  auto scenario = std::make_unique<mv::Scenario>();
  scenario->options.extent = 16;
  scenario->terrain.assign(std::size_t{16} * 16, 0u);
  for (int x = 1; x < 15; ++x) {
    mv::grid_set(scenario->terrain, 16, x, 8, true);
  }

  tess::PathAgentState blocker;
  blocker.position = tess::Coord3{8, 8, 0};
  blocker.has_goal = false;  // terminal
  scenario->agents.push_back(blocker);

  tess::PathAgentState mover;
  mover.position = tess::Coord3{2, 8, 0};
  mover.goal = tess::Coord3{13, 8, 0};
  mover.has_goal = true;
  mover.phase = tess::PathAgentPhase::Following;
  scenario->agents.push_back(mover);

  auto categories = mv::classify(*scenario, false);
  EXPECT_EQ(categories[0], mv::Category::Arrived);
  EXPECT_EQ(categories[1], mv::Category::Sealed)
      << "a terminal agent cutting the only corridor must read as sealed";

  // Wedged: the same geometry, but the blocker still has a goal, so it
  // is a live obstruction rather than a permanent one. A BFS under the
  // terminal set alone cannot tell these apart, which is precisely why
  // the wedge category exists.
  scenario->agents[0].has_goal = true;
  scenario->agents[0].goal = tess::Coord3{2, 8, 0};
  scenario->agents[0].phase = tess::PathAgentPhase::Following;
  categories = mv::classify(*scenario, false);
  EXPECT_EQ(categories[1], mv::Category::Wedged)
      << "a live blocker must not be reported as an unsolvable seal";
}

TEST(MovementScenarios, ClassifierSeparatesGoalOccupiedFromSealed) {
  // A goal tile held by a terminal agent is an assignment failure that
  // reassignment would fix, not a corridor seal that no mover can
  // solve. Merging them would dilute the fungible-goal experiment.
  auto scenario = std::make_unique<mv::Scenario>();
  scenario->options.extent = 16;
  scenario->terrain.assign(std::size_t{16} * 16, 0u);
  for (int x = 1; x < 15; ++x) {
    mv::grid_set(scenario->terrain, 16, x, 8, true);
  }

  tess::PathAgentState squatter;
  squatter.position = tess::Coord3{13, 8, 0};
  squatter.has_goal = false;
  scenario->agents.push_back(squatter);

  tess::PathAgentState mover;
  mover.position = tess::Coord3{2, 8, 0};
  mover.goal = tess::Coord3{13, 8, 0};
  mover.has_goal = true;
  mover.phase = tess::PathAgentPhase::Following;
  scenario->agents.push_back(mover);

  const auto categories = mv::classify(*scenario, false);
  EXPECT_EQ(categories[1], mv::Category::GoalOccupied);
}

TEST(MovementScenarios, AGoalBuriedInTerrainIsSealedNotReachable) {
  // The remaining way a goal can be unreachable: the goal tile is not
  // passable terrain at all, so no BFS from it can start. Found by
  // mutation testing -- inverting the goal-open guard left every other
  // assertion in this file green, which meant the branch was decorative.
  auto scenario = std::make_unique<mv::Scenario>();
  scenario->options.extent = 16;
  scenario->terrain.assign(std::size_t{16} * 16, 0u);
  for (int x = 1; x < 15; ++x) {
    mv::grid_set(scenario->terrain, 16, x, 8, true);
  }
  // The goal sits off the corridor, in solid terrain.
  mv::grid_set(scenario->terrain, 16, 13, 3, false);

  tess::PathAgentState mover;
  mover.position = tess::Coord3{2, 8, 0};
  mover.goal = tess::Coord3{13, 3, 0};
  mover.has_goal = true;
  mover.phase = tess::PathAgentPhase::Following;
  scenario->agents.push_back(mover);

  const auto categories = mv::classify(*scenario, false);
  EXPECT_EQ(categories[0], mv::Category::Sealed)
      << "an impassable goal tile must not read as a live wedge";
}

TEST(MovementScenarios, CensoringIsVisibleRatherThanSilent) {
  // A run that hits the safety cap knows nothing about its unarrived
  // agents. Reporting them as movement failures would be a claim the
  // experiment never earned.
  auto scenario = mv::build_scenario(mv::Family::Ring, 0);
  scenario->options.tick_cap = 2;
  scenario->options.wedge_ticks = 1000;
  auto rank = mv::route_attachment_ranking(*scenario);
  const auto outcome = mv::settle_with_pibt(*scenario, rank);
  EXPECT_TRUE(outcome.censored);
  EXPECT_FALSE(outcome.fixpoint);
  EXPECT_EQ(outcome.count(mv::Category::Sealed), 0u);
  EXPECT_EQ(outcome.count(mv::Category::Wedged), 0u);
  EXPECT_GT(outcome.count(mv::Category::Censored), 0u);
}

TEST(MovementScenarios, AFixpointRunNeverReportsCensoredAgents) {
  // The complement: once a run terminates on quiescence or a wedge,
  // every unarrived agent has a real category and none is censored.
  for (const auto family : {mv::Family::Adversarial, mv::Family::Warehouse}) {
    auto scenario = mv::build_scenario(family, 0);
    auto rank = mv::route_attachment_ranking(*scenario);
    const auto outcome = mv::settle_with_pibt(*scenario, rank);
    if (!outcome.fixpoint) {
      continue;  // capped runs are covered by the censoring test
    }
    EXPECT_EQ(outcome.count(mv::Category::Censored), 0u)
        << mv::family_name(family);
    EXPECT_EQ(outcome.categories.size(), scenario->agents.size());
  }
}

TEST(MovementScenarios, TheHarnessDoesNotManufactureWedgesOnTheRing) {
  // The ring lattice is where this tier's own pinned regression proves
  // it solves the whole population, so a wedge reported here is the
  // harness's fault, not the mover's.
  //
  // This is a regression test for a real defect: an earlier revision
  // settled agents without invalidating retained routes, so an agent
  // whose route crossed a newly settled tile went Blocked with
  // last_result == Found, which the scoped-submission filter excludes
  // from replanning permanently. It then parked until the wedge rule
  // fired. That artifact classified 354 of 960 ring agents as wedged --
  // an order of magnitude larger than the effects C1 is hunting, and it
  // does not cancel across arms, because C1 changes the very ranking
  // that decides park-versus-detour.
  for (unsigned trial = 0; trial < 5; ++trial) {
    auto scenario = mv::build_scenario(mv::Family::Ring, trial);
    auto rank = mv::route_attachment_ranking(*scenario);
    const auto outcome = mv::settle_with_pibt(*scenario, rank);
    EXPECT_EQ(outcome.count(mv::Category::Wedged), 0u)
        << "ring trial " << trial << " reported a wedge the tier should "
        << "not produce; retained routes are probably going stale";
    EXPECT_EQ(outcome.count(mv::Category::Censored), 0u)
        << "ring trial " << trial;
    EXPECT_TRUE(outcome.all_arrived()) << "ring trial " << trial;
  }
}

TEST(MovementScenarios, WedgeDetectionTerminatesARealDeadlock) {
  // The wedge rule terminates every run that ends with residuals, not
  // just live wedges -- a sealed agent keeps its goal, so `all_arrived`
  // never fires. Deleting the rule therefore sends every such run to the
  // safety cap, and nothing else in this file would notice. This test is
  // what makes the rule falsifiable.
  //
  // A head-on pair in a one-wide corridor under Forbid cannot resolve:
  // neither agent may leave its route, and swapping is disallowed.
  auto scenario = std::make_unique<mv::Scenario>();
  scenario->options.extent = 16;
  scenario->options.agent_count = 2;
  scenario->options.swap = tess::SwapPolicy::Forbid;
  scenario->options.tick_cap = 500;
  scenario->terrain.assign(std::size_t{16} * 16, 0u);
  for (int x = 2; x < 10; ++x) {
    mv::grid_set(scenario->terrain, 16, x, 8, true);
  }
  mv::fill_world(scenario->world);
  for (int x = 2; x < 10; ++x) {
    scenario->world.field<mv::PassableTag>(tess::Coord3{x, 8, 0}) = true;
  }

  const auto place = [&](tess::Coord3 at, tess::Coord3 goal) {
    tess::PathAgentState agent;
    agent.position = at;
    scenario->world.field<mv::OccupancyTag>(at) = true;
    tess::set_path_agent_goal(scenario->state, agent, goal);
    scenario->agents.push_back(agent);
  };
  place(tess::Coord3{3, 8, 0}, tess::Coord3{9, 8, 0});
  place(tess::Coord3{4, 8, 0}, tess::Coord3{2, 8, 0});

  auto rank = mv::route_attachment_ranking(*scenario);
  const auto outcome = mv::settle_with_pibt(*scenario, rank);

  EXPECT_TRUE(outcome.fixpoint)
      << "the run must terminate on the wedge rule, not the safety cap";
  EXPECT_FALSE(outcome.censored);
  EXPECT_LT(outcome.ticks, scenario->options.tick_cap)
      << "reaching the cap means the wedge rule never fired";
  EXPECT_GT(outcome.count(mv::Category::Wedged), 0u)
      << "a live mutual block must classify as wedged, not sealed";
  EXPECT_EQ(outcome.count(mv::Category::Sealed), 0u)
      << "neither agent is terminal, so nothing is sealed";
  // Pin the swap counters against an expected value rather than only
  // against each other. Two identical runs agree under any accumulation
  // bug, including summing denials into the swap count; a head-on pair
  // under Forbid is the case where the two must differ.
  EXPECT_EQ(outcome.swaps, 0u) << "Forbid must not report a swap";
  EXPECT_GT(outcome.swaps_denied, 0u)
      << "a head-on pair under Forbid must record denied swaps";
}

TEST(MovementScenarios, WedgeDetectionRequiresSustainedNoProgress) {
  // The stopping rule's false-positive mode is a run that pauses briefly
  // and then resolves, so a single stalled tick must not end a run.
  auto scenario = mv::build_scenario(mv::Family::Warehouse, 0);
  ASSERT_GT(scenario->options.wedge_ticks, 1)
      << "a one-tick wedge rule would stop on ordinary contention";
}

}  // namespace
