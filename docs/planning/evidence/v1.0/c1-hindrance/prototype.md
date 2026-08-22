# C1 prototype, recorded as source

The arm was rejected, so under the plan's rule -- "Rejected code is removed
from the branch; the retained artifact is the evidence record, not dead
production machinery" -- it does not stay in the tree. This matches the P1
seam stand-in's treatment in the sibling evidence directory.

Two defects found in review are fixed here rather than preserved verbatim,
because a recorded artifact that a later reader might build should not carry
known-wrong code: the `kNearestLimit` comment claimed the clamp fails loudly
when it saturates silently, and the ordering test compared only tile-scan
neighbours, which does not imply the all-pairs preservation its comment
claimed.

The `before_tick` hook these depend on stays in `tests/movement_scenarios.h`.
It is substrate rather than the rejected mechanism, and the measurement
programs in `programs.md` call it.

## `hindrance_ranking.h`

```cpp
// C1 prototype: a hindrance tie-break composed into the PIBT ranking
// oracle. Harness support only, never a public header, and deliberately
// no change to `include/tess/sim/pibt_movement.h` -- the plan's rules
// keep a prototype private until evidence justifies authority, and a
// policy parameter on the tier would put this in a public header before
// any evidence exists.
//
// The tier already breaks rank ties by enumeration order, so composing
//
//     composed = rank * K + min(hindrance, K - 1)
//
// makes route attachment the primary key, hindrance the secondary, and
// enumeration order the tertiary. Both arms are then two oracle objects
// in one binary rather than two builds.
#pragma once

#include <tess/tess.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "movement_scenarios.h"

namespace tess_test::movement {

// Hindrance of a tile is the number of OTHER active agents whose
// retained route's next point is that tile.
//
// An earlier definition -- "agents for which this tile is currently
// their best-ranked candidate" -- was withdrawn as incoherent with the
// algorithm. PIBT computes ranks lazily, per agent, only when that agent
// begins deciding, and discards the frame afterwards. Under the reading
// "ranks retained from earlier deciders" no such store exists, so the
// highest-priority decider -- the one with the most influence on the
// tick -- would always see hindrance zero. Under the reading "decided
// agents' chosen tiles", every decided destination is already claimed
// and vertex-rejected before a tie-break could see it, so the tie-break
// could never fire.
//
// Retained routes are fixed before any decision in a pass, so this
// definition is computable where the tie-break applies, is independent
// of decision order, and adds no ranking-oracle calls.
class HindranceIndex {
 public:
  // Rebuilt once per pass, not once per tick: with `max_steps > 1` the
  // tier runs several passes and route positions advance between them.
  void rebuild(std::span<const tess::PathAgentState> agents,
               const tess::PathAgentRoutes& routes) {
    keys_.clear();
    counts_.clear();
    for (std::size_t i = 0; i < agents.size(); ++i) {
      const auto next = next_route_point(agents, routes, i);
      if (!next.has_value()) {
        continue;
      }
      keys_.push_back(tess::tile_key<Shape2D>(*next).value);
    }
    std::sort(keys_.begin(), keys_.end());
    // Collapse to (key, count) so a lookup is a binary search rather
    // than a scan, matching the occupant-index pattern the tier already
    // uses for its own scratch.
    counts_.assign(keys_.size(), 0u);
    std::size_t out = 0;
    for (std::size_t i = 0; i < keys_.size();) {
      std::size_t j = i;
      while (j < keys_.size() && keys_[j] == keys_[i]) {
        ++j;
      }
      keys_[out] = keys_[i];
      counts_[out] = static_cast<std::uint32_t>(j - i);
      ++out;
      i = j;
    }
    keys_.resize(out);
    counts_.resize(out);
  }

  // `self_next` is excluded so an agent's own next-route-point never
  // counts toward the hindrance of its own candidates. Without that an
  // agent would penalise exactly the tile it is trying to reach.
  [[nodiscard]] auto at(tess::Coord3 tile, bool exclude_self) const
      -> std::uint32_t {
    const auto key = tess::tile_key<Shape2D>(tile).value;
    const auto it = std::lower_bound(keys_.begin(), keys_.end(), key);
    if (it == keys_.end() || *it != key) {
      return 0;
    }
    const auto index = static_cast<std::size_t>(it - keys_.begin());
    const auto raw = counts_[index];
    return exclude_self && raw > 0 ? raw - 1 : raw;
  }

  [[nodiscard]] static auto next_route_point(
      std::span<const tess::PathAgentState> agents,
      const tess::PathAgentRoutes& routes, std::size_t agent)
      -> std::optional<tess::Coord3> {
    if (agent >= routes.routes.size() || !agents[agent].has_goal) {
      return std::nullopt;
    }
    const auto& route = routes.routes[agent];
    const auto next = agents[agent].path_index + 1;
    if (next >= route.size()) {
      return std::nullopt;
    }
    return route[next];
  }

 private:
  std::vector<std::uint64_t> keys_;
  std::vector<std::uint32_t> counts_;
};

// The candidate arm's oracle. The control arm is
// `tess::RouteAttachmentRanking` used directly, so the two differ only
// by this composition.
// The index is rebuilt lazily, on the first ranking call of a pass,
// rather than before the tick. Routes do not exist until the tier's
// planning pass runs, and planning happens inside the same call as
// movement, so anything built before the call is a pass stale -- on the
// very first tick it is empty outright. Building on first use puts the
// rebuild after planning and before any decision, which is what makes
// "fixed before any decision in the pass" true rather than aspirational.
//
// This stays order-independent: the index is a pure function of the
// agent array and the retained routes, both unchanged by ranking calls,
// so whichever agent triggers the build gets the same table.
struct HindranceRanking {
  static constexpr std::uint32_t kScale = 8;
  // `nearest` is clamped before scaling so the detached branch cannot
  // overflow. On a 64x64 lattice the Manhattan diameter is 126, so the
  // limit is never approached. Note the clamp saturates SILENTLY: it
  // does not detect a larger family, it merges orderings above the
  // limit. Any larger fixture shape must recheck the composition rather
  // than rely on this to complain.
  static constexpr std::uint32_t kNearestLimit = 1u << 20u;

  tess::RouteAttachmentRanking base{};
  std::span<const tess::PathAgentState> agents{};
  const tess::PathAgentRoutes* routes = nullptr;
  bool enabled = true;

  /// Call once per pass, before the tier's advance, to retire the
  /// previous pass's table.
  void begin_pass() const { built_ = false; }

  [[nodiscard]] auto index() const -> const HindranceIndex& {
    if (!built_) {
      hindrance_.rebuild(agents, *routes);
      built_ = true;
    }
    return hindrance_;
  }

  [[nodiscard]] auto operator()(std::size_t agent, tess::Coord3 candidate) const
      -> std::uint32_t {
    const auto rank = base(agent, candidate);
    if (!enabled || routes == nullptr) {
      return rank;
    }
    const auto& hindrance = index();
    const auto self_next =
        HindranceIndex::next_route_point(agents, *routes, agent);
    const auto is_self = self_next.has_value() && self_next->x == candidate.x &&
                         self_next->y == candidate.y &&
                         self_next->z == candidate.z;
    const auto h = std::min(hindrance.at(candidate, is_self), kScale - 1);

    // Attached and detached scores occupy disjoint ranges. Scaling the
    // detached range would overflow, so the composition is piecewise;
    // the attached range's scaled maximum stays far below the detached
    // base, so every attached candidate still sorts below every
    // detached one.
    constexpr auto kDetached = tess::RouteAttachmentRanking::kDetachedBase;
    if (rank < kDetached) {
      return rank * kScale + h;
    }
    const auto nearest = std::min(rank - kDetached, kNearestLimit);
    return kDetached + nearest * kScale + h;
  }

  // Public so the struct stays an aggregate and both arms remain plain
  // brace-initialised objects. Treat as internal: `begin_pass` is the
  // only supported way to retire the table.
  mutable HindranceIndex hindrance_{};
  mutable bool built_ = false;
};

}  // namespace tess_test::movement
```

## `tess_hindrance_tiebreak_test.cc`

```cpp
#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "hindrance_ranking.h"
#include "movement_scenarios.h"

namespace {

namespace mv = tess_test::movement;

// Builds both arms over one scenario so a caller cannot accidentally
// compare different instances.
struct Arms {
  std::unique_ptr<mv::Scenario> control{};
  std::unique_ptr<mv::Scenario> candidate{};
};

auto build_arms(mv::Family family, unsigned trial) -> Arms {
  return Arms{mv::build_scenario(family, trial),
              mv::build_scenario(family, trial)};
}

auto run_control(mv::Scenario& scenario) -> mv::Outcome {
  auto rank = mv::route_attachment_ranking(scenario);
  return mv::settle_with_pibt(scenario, rank);
}

auto run_candidate(mv::Scenario& scenario) -> mv::Outcome {
  mv::HindranceRanking rank{
      mv::route_attachment_ranking(scenario),
      std::span<const tess::PathAgentState>(scenario.agents),
      &scenario.state.routes, true};
  return mv::settle_with_pibt(scenario, rank,
                              [&rank](mv::Scenario&) { rank.begin_pass(); });
}

TEST(HindranceTieBreak, UniformHindranceReproducesTheIncumbentOrdering) {
  // The claim under test is that hindrance is a tie-break, not a
  // re-ranking. With an empty index every candidate has hindrance zero,
  // so the composition must preserve the incumbent's ordering exactly --
  // otherwise the arm changes which candidates are considered good, not
  // merely which of several equally good ones is taken.
  auto scenario = mv::build_scenario(mv::Family::Warehouse, 0);
  const auto base = mv::route_attachment_ranking(*scenario);
  // No routes are planned yet, so every tile has hindrance zero and the
  // composition must be order-preserving on its own.
  const mv::HindranceRanking composed{
      base, std::span<const tess::PathAgentState>(scenario->agents),
      &scenario->state.routes, true};

  const auto extent = scenario->options.extent;
  for (std::size_t agent = 0; agent < scenario->agents.size(); ++agent) {
    std::vector<std::pair<std::uint32_t, std::uint32_t>> pairs;
    for (int y = 0; y < extent; ++y) {
      for (int x = 0; x < extent; ++x) {
        const auto tile = tess::Coord3{x, y, 0};
        pairs.emplace_back(base(agent, tile), composed(agent, tile));
      }
    }
    // Sorted by base rank first, so adjacent checks chain by
    // transitivity into an all-pairs claim. Comparing tile-scan
    // neighbours alone would pass on orderings like base 1,3,2 with
    // composed 10,20,5, which inverts the (1,2) pair.
    std::sort(pairs.begin(), pairs.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    // Every strict ordering in the incumbent must survive composition.
    for (std::size_t i = 1; i < pairs.size(); ++i) {
      const auto& a = pairs[i - 1];
      const auto& b = pairs[i];
      if (a.first < b.first) {
        EXPECT_LT(a.second, b.second) << "agent " << agent;
      } else if (a.first > b.first) {
        EXPECT_GT(a.second, b.second) << "agent " << agent;
      } else {
        EXPECT_EQ(a.second, b.second) << "agent " << agent;
      }
    }
  }
}

TEST(HindranceTieBreak, CompositionKeepsAttachedBelowDetached) {
  // The composition is piecewise because scaling the detached range
  // would overflow. That is only sound while the scaled attached maximum
  // stays below the detached base, so pin the gap rather than trusting
  // the arithmetic to stay true if a larger family is ever added.
  constexpr auto kDetached = tess::RouteAttachmentRanking::kDetachedBase;
  constexpr auto kScale = mv::HindranceRanking::kScale;
  // Worst-case attached rank on the committed 64x64 shape: attach_radius
  // plus the longest possible route through every tile.
  constexpr std::uint32_t kWorstAttached = 1 + (64 * 64);
  EXPECT_LT(kWorstAttached * kScale + (kScale - 1), kDetached)
      << "a larger fixture shape would break the piecewise composition";
}

TEST(HindranceTieBreak, HindranceCountsOtherAgentsNextRoutePoints) {
  // The definition is load-bearing and was rewritten once, so pin it
  // directly rather than only through end-to-end behaviour.
  // Routes do not exist until the tier plans, so run the scenario to a
  // settled state first and rebuild against real retained routes. An
  // index built at construction time would be empty, which is exactly
  // the flaw this test caught in the first draft.
  auto scenario = mv::build_scenario(mv::Family::Ring, 0);
  {
    scenario->options.tick_cap = 3;
    scenario->options.wedge_ticks = 1000;
    auto warm = mv::route_attachment_ranking(*scenario);
    (void)mv::settle_with_pibt(*scenario, warm);
  }
  mv::HindranceIndex index;
  index.rebuild(std::span<const tess::PathAgentState>(scenario->agents),
                scenario->state.routes);

  // Recompute independently from the agent array.
  std::vector<tess::Coord3> nexts;
  for (std::size_t i = 0; i < scenario->agents.size(); ++i) {
    const auto next = mv::HindranceIndex::next_route_point(
        std::span<const tess::PathAgentState>(scenario->agents),
        scenario->state.routes, i);
    if (next.has_value()) {
      nexts.push_back(*next);
    }
  }
  ASSERT_FALSE(nexts.empty()) << "no agent has a next route point";

  for (const auto tile : nexts) {
    const auto expected = static_cast<std::uint32_t>(
        std::count_if(nexts.begin(), nexts.end(), [&](tess::Coord3 c) {
          return c.x == tile.x && c.y == tile.y && c.z == tile.z;
        }));
    EXPECT_EQ(index.at(tile, false), expected);
    EXPECT_EQ(index.at(tile, true), expected - 1)
        << "self-exclusion must remove exactly one";
  }
}

TEST(HindranceTieBreak, BothArmsReplayIdentically) {
  // Determinism is a gate, not a result: a non-deterministic arm makes
  // every later comparison meaningless.
  for (const auto family : {mv::Family::Warehouse, mv::Family::Colony}) {
    auto first = build_arms(family, 0);
    auto second = build_arms(family, 0);
    const auto a = run_candidate(*first.candidate);
    const auto b = run_candidate(*second.candidate);
    EXPECT_EQ(a.ticks, b.ticks) << mv::family_name(family);
    EXPECT_EQ(a.categories, b.categories) << mv::family_name(family);
  }
}

TEST(HindranceTieBreak, NoSeedRegressesItsTerminalClassification) {
  // The pre-registered rejection condition. A seed may improve
  // classification -- that is how this tier is supposed to help -- but
  // none may regress: arrived to residual, or wedged to sealed.
  const auto rank_of = [](mv::Category c) {
    switch (c) {
      case mv::Category::Arrived:
        return 0;
      // A live wedge and an occupied goal are the same severity: both
      // are residuals a different policy could plausibly resolve.
      case mv::Category::Wedged:
      case mv::Category::GoalOccupied:
        return 1;
      case mv::Category::Sealed:
        return 2;
      case mv::Category::Censored:
        return 3;
    }
    return 3;
  };
  for (const auto family : {mv::Family::Warehouse, mv::Family::Ring}) {
    for (unsigned trial = 0; trial < 3; ++trial) {
      auto arms = build_arms(family, trial);
      const auto control = run_control(*arms.control);
      const auto candidate = run_candidate(*arms.candidate);
      ASSERT_EQ(control.categories.size(), candidate.categories.size());
      for (std::size_t i = 0; i < control.categories.size(); ++i) {
        EXPECT_LE(rank_of(candidate.categories[i]),
                  rank_of(control.categories[i]))
            << mv::family_name(family) << " trial " << trial << " agent " << i
            << ": " << mv::category_name(control.categories[i]) << " -> "
            << mv::category_name(candidate.categories[i]);
      }
    }
  }
}

}  // namespace
```
