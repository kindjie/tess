# C5x screen program: recorded source

Drives the demo model's public pricing mode; compile from the
repository root:

```
c++ -std=c++23 -O2 -DNDEBUG -Iinclude -Ibuild/dev/generated/include \
  -Iexamples/web_colony c5x_screen.cc \
  examples/web_colony/colony_model.cc -o c5x_screen
```

```cpp
// C5x accounting screen (issue #269, amendments 1-2): canonical vs the
// seven registered pricing policies on the demo's own model, driven
// entirely through the PUBLIC surface (set_congestion_pricing), so the
// browser demo and this evidence run the identical code path. Seven
// scenarios x populations {256, 1024}; per-policy replay for the
// determinism gate; safety gates (C5 G1-G3 shape) enforced inline.
// Usage: c5x_screen [scenario] for one scenario, none for all seven.
#include <array>
#include <cstdio>
#include <string_view>
#include <vector>

#include "colony_endpoint_guard_fixture.h"
#include "colony_model.h"

namespace wc = tess::examples::web_colony;

namespace {

int failures = 0;
#define CHECK(cond, ...)                                              \
  do {                                                                \
    if (!(cond)) {                                                    \
      ++failures;                                                     \
      std::printf("GATE FAIL line %d: %s ", __LINE__, #cond);         \
      std::printf(__VA_ARGS__);                                       \
      std::printf("\n");                                              \
    }                                                                 \
  } while (0)

constexpr std::array<const char*, 14> kArmNames = {
    "canonical", "prox1",   "prox2",  "self",      "decay",
    "stalled",   "demand",  "queue",  "peak1",     "cool",
    "queue2",    "stallpeak", "stallcool", "peakcool"};

std::pair<std::size_t, std::size_t> queue_walls(wc::ColonyModel& model,
                                                std::string_view scenario) {
  auto accepted = std::size_t{0};
  auto attempted = std::size_t{0};
  if (scenario == "browser-guard") {
    for (const auto& [x, y] : wc::kEndpointGuardReproductionWalls) {
      ++attempted;
      accepted += model.queue_wall(x, y) ? 1u : 0u;
    }
    return {accepted, attempted};
  }
  if (scenario == "goal-wall") {
    for (auto y = 0; y < 96; ++y) {
      ++attempted;
      accepted += model.queue_wall(wc::width - 19, y) ? 1u : 0u;
    }
    return {accepted, attempted};
  }
  for (auto y = 0; y < wc::height; ++y) {
    const auto wall = scenario == "tip" ? y >= 32
                      : scenario == "two-gates"
                          ? !((y >= 24 && y < 32) || (y >= 96 && y < 104))
                      : scenario == "four-gates"
                          ? !((y >= 16 && y < 24) || (y >= 48 && y < 56) ||
                              (y >= 80 && y < 88) || (y >= 112 && y < 120))
                          : false;
    if (wall) {
      ++attempted;
      accepted += model.queue_wall(64, y) ? 1u : 0u;
    }
  }
  return {accepted, attempted};
}

struct Terminal {
  int arrived = 0;
  int crowd_blocked = 0;
  int unreachable = 0;
  int ticks = 0;
  bool turnaround = false;
  // Planning-load proxy (amendment 3): the sum of pending plans over
  // the run, via the public planning_pending().
  long long planning_load = 0;
};

Terminal run(std::string_view scenario, int agents, int policy) {
  wc::ColonyModel model{agents};
  model.set_spread_congested_routes(false);
  model.set_congestion_pricing(policy);
  Terminal out;
  const bool incremental = scenario == "browser-incremental";
  std::size_t incremental_wall = 0;
  constexpr std::size_t kIncrementalTotal =
      std::size(wc::kEndpointGuardReproductionWalls);
  const auto incremental_pending = [&] {
    return incremental && incremental_wall < kIncrementalTotal;
  };
  for (int tick = 0;
       tick < 5000 && (!model.turnaround_ready() || incremental_pending());
       ++tick) {
    if (tick == 4 && !incremental) {
      const auto [accepted, attempted] = queue_walls(model, scenario);
      CHECK(accepted == attempted, "%.*s N=%d walls %zu/%zu",
            (int)scenario.size(), scenario.data(), agents, accepted,
            attempted);
    }
    if (tick >= 4 && incremental_pending()) {
      constexpr std::size_t kWallsPerTick = 4;
      std::size_t accepted_this_tick = 0;
      while (accepted_this_tick < kWallsPerTick && incremental_pending()) {
        const auto [x, y] =
            wc::kEndpointGuardReproductionWalls[incremental_wall];
        if (!model.queue_wall(x, y)) {
          break;
        }
        ++accepted_this_tick;
        ++incremental_wall;
      }
    }
    (void)model.tick(0.05);
    out.planning_load += model.planning_pending();
    out.ticks = tick + 1;
  }
  if (incremental) {
    CHECK(incremental_wall == kIncrementalTotal,
          "browser-incremental N=%d walls admitted %zu/%zu", agents,
          incremental_wall, kIncrementalTotal);
  }
  out.turnaround = model.turnaround_ready();
  out.arrived = model.arrived();
  out.crowd_blocked = model.crowd_blocked();
  out.unreachable = model.unreachable();
  return out;
}

}  // namespace

int main(int argc, char** argv) {
  const std::array<std::string_view, 7> all_scenarios = {
      "open",      "tip",           "two-gates", "four-gates",
      "goal-wall", "browser-guard", "browser-incremental"};
  std::vector<std::string_view> scenarios;
  if (argc > 1) {
    scenarios.push_back(argv[1]);
  } else {
    scenarios.assign(all_scenarios.begin(), all_scenarios.end());
  }
  bool any_disqualified = false;
  for (const auto scenario : scenarios) {
    for (const int agents : {256, 1024}) {
      const auto canonical = run(scenario, agents, 0);
      const bool canon_complete =
          canonical.turnaround && canonical.arrived == agents;
      std::printf("cell,%.*s,%d,canonical,%d,%d,%d,%d,%d,-,%lld\n",
                  (int)scenario.size(), scenario.data(), agents,
                  canonical.arrived, canonical.crowd_blocked,
                  canonical.unreachable, canonical.ticks,
                  canonical.turnaround ? 1 : 0, canonical.planning_load);
      std::fflush(stdout);
      for (int policy = 1; policy <= 13; ++policy) {
        const auto priced = run(scenario, agents, policy);
        const auto replay = run(scenario, agents, policy);
        CHECK(priced.arrived == replay.arrived &&
                  priced.crowd_blocked == replay.crowd_blocked &&
                  priced.unreachable == replay.unreachable &&
                  priced.ticks == replay.ticks &&
                  priced.turnaround == replay.turnaround,
              "%.*s N=%d %s replay", (int)scenario.size(), scenario.data(),
              agents, kArmNames[static_cast<std::size_t>(policy)]);
        const bool priced_complete =
            priced.turnaround && priced.arrived == agents;
        const char* gate = "pass";
        if (canon_complete) {
          if (!priced_complete || priced.crowd_blocked != 0 ||
              priced.unreachable != 0) {
            gate = "SAFETY-FAIL";
            any_disqualified = true;
          }
        } else if (priced.arrived < canonical.arrived ||
                   priced.crowd_blocked > canonical.crowd_blocked ||
                   priced.unreachable > canonical.unreachable) {
          gate = "NOWORSE-FAIL";
          any_disqualified = true;
        }
        std::printf("cell,%.*s,%d,%s,%d,%d,%d,%d,%d,%s,%lld\n",
                    (int)scenario.size(), scenario.data(), agents,
                    kArmNames[static_cast<std::size_t>(policy)],
                    priced.arrived, priced.crowd_blocked, priced.unreachable,
                    priced.ticks, priced.turnaround ? 1 : 0, gate,
                    priced.planning_load);
        std::fflush(stdout);
      }
    }
  }
  std::printf("mechanical checks: %s (%d)\n",
              failures == 0 ? "clean" : "FAILED", failures);
  std::printf("safety gates: %s\n",
              any_disqualified ? "DISQUALIFICATIONS (see cells)" : "ALL PASS");
  return failures == 0 ? 0 : 1;
}
```
