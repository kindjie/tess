# C5 measurement programs: recorded source

Both programs compile against the repository as measured (main
`b87900a5`); neither is a maintained tool. Rebuild lines are exact.

## c5_colony_leg.cc -- the demo-classifier leg (amendment-3 matrix)

The amendment-3 program: six scenarios (browser-guard included via the
demo's own guard-wall fixture header) x all 64 supported populations,
gates G1-G5 enforced inline, machine-parseable per-cell output, one
scenario per invocation for parallel capture. The superseded
amendment-2 two-population version is recoverable from this file's git
history; its capture remains `colony-leg.txt`. Compile from the
repository root:

```
c++ -std=c++23 -O2 -DNDEBUG -Iinclude -Ibuild/dev/generated/include \
  -Iexamples/web_colony c5_colony_leg.cc \
  examples/web_colony/colony_model.cc -o c5_colony_leg
```

The Deck leg runs the identical source cross-compiled for x86-64 Linux
(gcc 14, `-static`, same flags); tick counts and classifications are
deterministic simulation outputs, so G6 compares the emitted `cell,`
tables byte-for-byte.

```cpp
// C5 demo-classifier leg, amendment-3 matrix (issue #256): the
// execution plan requires C5's terminal-classification gate to be
// judged by the web-colony demo's own recovery classifier -- arrived,
// crowd-blocked, durably unreachable -- over EVERY supported
// population. Six scenarios (the native CLI's own set, browser-guard
// included) x all 64 supported populations (16..1024 step 16) x
// {canonical, priced, priced-replay}; the shipped spread mode is
// context only at {256, 1024}. Gates G1-G5 from amendment 3 are
// enforced inline; pooled tick value (the pre-declared gm/CI rule) is
// computed by the recorded post-processing step over the per-cell CSV
// this program emits. Usage: c5_colony_leg [scenario] to run one
// scenario (for parallel capture), no argument for all six.
#include <array>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <vector>

#include "colony_endpoint_guard_fixture.h"
#include "colony_model_internal.h"

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

// Returns {accepted, attempted}; amendment-3 gate G5 requires them
// equal in every arm of every cell.
std::pair<std::size_t, std::size_t> queue_walls(wc::ColonyModel& model,
                                                std::string_view scenario) {
  auto accepted = std::size_t{0};
  auto attempted = std::size_t{0};
  if (scenario == "browser-guard") {
    for (const auto& [x, y] : wc::kEndpointGuardReproductionWalls) {
      ++attempted;
      accepted += model.set_wall(x, y, true) ? 1u : 0u;
    }
    return {accepted, attempted};
  }
  if (scenario == "goal-wall") {
    for (auto y = 0; y < 96; ++y) {
      ++attempted;
      accepted += model.set_wall(wc::kWidth - 19, y, true) ? 1u : 0u;
    }
    return {accepted, attempted};
  }
  for (auto y = 0; y < wc::kHeight; ++y) {
    const auto wall = scenario == "tip" ? y >= 32
                      : scenario == "two-gates"
                          ? !((y >= 24 && y < 32) || (y >= 96 && y < 104))
                      : scenario == "four-gates"
                          ? !((y >= 16 && y < 24) || (y >= 48 && y < 56) ||
                              (y >= 80 && y < 88) || (y >= 112 && y < 120))
                          : false;
    if (wall) {
      ++attempted;
      accepted += model.set_wall(64, y, true) ? 1u : 0u;
    }
  }
  return {accepted, attempted};
}

// The pre-registered bounded policy, applied through the demo's own
// world: every 4 fixed ticks, price = 1 + min(3, live agents within
// Manhattan 1), written with the versioned-edit contract. Cost is not
// topology-relevant, so passability freshness is untouched by design.
template <typename Impl>
void apply_pricing(Impl& impl) {
  auto& world = impl.world;
  std::vector<std::uint8_t> pressure(
      static_cast<std::size_t>(wc::kWidth) * wc::kHeight, 0);
  const auto bump = [&](std::int64_t x, std::int64_t y) {
    if (x < 0 || y < 0 || x >= wc::kWidth || y >= wc::kHeight) return;
    auto& cell = pressure[static_cast<std::size_t>(y) * wc::kWidth +
                          static_cast<std::size_t>(x)];
    if (cell < 255) ++cell;
  };
  for (const auto& agent : impl.agents) {
    if (!agent.has_goal) continue;
    bump(agent.position.x, agent.position.y);
    bump(agent.position.x + 1, agent.position.y);
    bump(agent.position.x - 1, agent.position.y);
    bump(agent.position.x, agent.position.y + 1);
    bump(agent.position.x, agent.position.y - 1);
  }
  using Shape = wc::Shape;
  constexpr auto chunk_total = tess::ShapeTraits<Shape>::chunk_count;
  std::vector<bool> changed(chunk_total, false);
  for (int y = 0; y < wc::kHeight; ++y) {
    for (int x = 0; x < wc::kWidth; ++x) {
      const tess::Coord3 c{x, y, 0};
      const auto p = pressure[static_cast<std::size_t>(y) * wc::kWidth +
                              static_cast<std::size_t>(x)];
      const auto price = static_cast<std::uint32_t>(1 + (p > 3 ? 3 : p));
      auto& field = world.template field<wc::CostTag>(c);
      if (field != price) {
        field = price;
        changed[static_cast<std::size_t>(
            tess::chunk_key<Shape>(tess::chunk_coord<Shape>(c)).value)] =
            true;
      }
    }
  }
  bool any = false;
  for (std::uint64_t k = 0; k < chunk_total; ++k) {
    if (changed[static_cast<std::size_t>(k)]) {
      world.mark_content_changed(tess::ChunkKey{k});
      any = true;
    }
  }
  if (any) {
    tess::mark_pathing_dirty(impl.tick_state);
  }
}

struct Terminal {
  int arrived = 0;
  int crowd_blocked = 0;
  int unreachable = 0;
  int ticks = 0;
  bool turnaround = false;
};

Terminal run(std::string_view scenario, int agents, bool priced,
             bool spread = false) {
  wc::ColonyModel model{agents};
  model.set_spread_congested_routes(spread);
  auto& impl = wc::ColonyModelNativeAccess::impl(model);
  Terminal out;
  for (int tick = 0; tick < 5000 && !model.turnaround_ready(); ++tick) {
    if (tick == 4) {
      const auto [accepted, attempted] = queue_walls(model, scenario);
      CHECK(accepted == attempted,
            "%.*s N=%d wall admission %zu/%zu (G5)", (int)scenario.size(),
            scenario.data(), agents, accepted, attempted);
    }
    (void)model.tick(0.05);
    if (priced && tick % 4 == 0) {
      apply_pricing(impl);
    }
    out.ticks = tick + 1;
  }
  out.turnaround = model.turnaround_ready();
  out.arrived = model.arrived();
  out.crowd_blocked = model.crowd_blocked();
  out.unreachable = model.unreachable();
  return out;
}

}  // namespace

int main(int argc, char** argv) {
  const std::array<std::string_view, 6> all_scenarios = {
      "open", "tip", "two-gates", "four-gates", "goal-wall",
      "browser-guard"};
  std::vector<std::string_view> scenarios;
  if (argc > 1) {
    scenarios.push_back(argv[1]);
  } else {
    scenarios.assign(all_scenarios.begin(), all_scenarios.end());
  }
  bool rejection = false;
  for (const auto scenario : scenarios) {
    for (int agents = 16; agents <= 1024; agents += 16) {
      const auto canonical = run(scenario, agents, false);
      const auto priced = run(scenario, agents, true);
      const auto priced_replay = run(scenario, agents, true);
      // G4: bit-identical priced replay.
      CHECK(priced.arrived == priced_replay.arrived &&
                priced.crowd_blocked == priced_replay.crowd_blocked &&
                priced.unreachable == priced_replay.unreachable &&
                priced.ticks == priced_replay.ticks,
            "%.*s N=%d replay (G4)", (int)scenario.size(), scenario.data(),
            agents);
      const bool canon_complete =
          canonical.turnaround && canonical.arrived == agents;
      const bool priced_complete =
          priced.turnaround && priced.arrived == agents;
      const char* gate = "pass";
      if (canon_complete) {
        // G1 retention; its violation is also G3 re-rejection.
        if (!priced_complete || priced.crowd_blocked != 0 ||
            priced.unreachable != 0) {
          gate = "G1-RETENTION-FAIL(G3)";
          rejection = true;
        }
      } else {
        // G2 no-worse where canonical fails.
        if (priced.arrived < canonical.arrived ||
            priced.crowd_blocked > canonical.crowd_blocked ||
            priced.unreachable > canonical.unreachable) {
          gate = "G2-NOWORSE-FAIL";
          rejection = true;
        }
      }
      std::printf(
          "cell,%.*s,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%s\n",
          (int)scenario.size(), scenario.data(), agents, canonical.arrived,
          canonical.crowd_blocked, canonical.unreachable, canonical.ticks,
          canonical.turnaround ? 1 : 0, priced.arrived,
          priced.crowd_blocked, priced.unreachable, priced.ticks,
          priced.turnaround ? 1 : 0, gate);
      std::fflush(stdout);
      // Context-only spread arm at the amendment-2 populations.
      if (agents == 256 || agents == 1024) {
        const auto spread = run(scenario, agents, false, true);
        std::printf("spread-context,%.*s,%d,%d,%d,%d,%d,%d\n",
                    (int)scenario.size(), scenario.data(), agents,
                    spread.arrived, spread.crowd_blocked,
                    spread.unreachable, spread.ticks,
                    spread.turnaround ? 1 : 0);
        std::fflush(stdout);
      }
    }
  }
  std::printf("mechanical+gate checks: %s (%d)\n",
              failures == 0 ? "clean" : "FAILED", failures);
  std::printf("classification gates G1-G3: %s\n",
              rejection ? "REJECTION (see cells)" : "PASS");
  return (failures == 0 && !rejection) ? 0 : 1;
}
```

## c5_arms.cc -- the substrate parity screen

Compile from the repository root (uses the C0 substrate header):

```
c++ -std=c++23 -O2 -DNDEBUG -Iinclude -Ibuild/dev/generated/include \
  -Itests c5_arms.cc -o c5_arms
```

Exit status covers the mechanical checks AND the experiment's
substrate-leg gate; that gate failed as registered, so the recorded
run exits nonzero by design (see the trailing note in `arms.txt`).

```cpp
// C5 dynamic congestion revalidation (issue #256): canonical vs the one
// pre-registered bounded price policy, on the full C0 substrate, one
// binary, interleaved. No library change exists in either arm: pricing
// writes the substrate's own CostTag field through the versioned edit
// channel, and the C0 Traveler class already composes FieldCost.
#include <cmath>
#include <cstdio>
#include <map>
#include <vector>

#include "movement_scenarios.h"

namespace {

namespace mv = tess_test::movement;
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

// The pre-registered policy: every 4 ticks, price each tile
// 1 + min(3, live agents within Manhattan 1), written through the
// versioned channel so planner and caches see a legitimate edit.
struct Pricing {
  int tick = 0;

  void operator()(mv::Scenario& scenario) {
    if (tick++ % 4 != 0) {
      return;
    }
    const auto extent = scenario.options.extent;
    std::vector<std::uint8_t> pressure(
        static_cast<std::size_t>(extent) * static_cast<std::size_t>(extent),
        0);
    const auto bump = [&](std::int64_t x, std::int64_t y) {
      if (x < 0 || y < 0 || x >= extent || y >= extent) return;
      auto& cell = pressure[static_cast<std::size_t>(y) *
                                static_cast<std::size_t>(extent) +
                            static_cast<std::size_t>(x)];
      if (cell < 255) ++cell;
    };
    for (const auto& agent : scenario.agents) {
      if (!agent.has_goal) continue;
      bump(agent.position.x, agent.position.y);
      bump(agent.position.x + 1, agent.position.y);
      bump(agent.position.x - 1, agent.position.y);
      bump(agent.position.x, agent.position.y + 1);
      bump(agent.position.x, agent.position.y - 1);
    }
    std::vector<bool> chunk_changed(tess::ShapeTraits<mv::Shape2D>::chunk_count, false);
    for (int y = 0; y < extent; ++y) {
      for (int x = 0; x < extent; ++x) {
        if (!mv::grid_at(scenario.terrain, extent, x, y)) continue;
        const tess::Coord3 c{x, y, 0};
        const auto p = pressure[static_cast<std::size_t>(y) *
                                    static_cast<std::size_t>(extent) +
                                static_cast<std::size_t>(x)];
        const auto price =
            static_cast<std::uint8_t>(1 + (p > 3 ? 3 : p));
        auto& field = scenario.world.field<mv::CostTag>(c);
        if (field != price) {
          field = price;
          const auto key =
              tess::chunk_key<mv::Shape2D>(tess::chunk_coord<mv::Shape2D>(c));
          chunk_changed[static_cast<std::size_t>(key.value)] = true;
        }
      }
    }
    bool any = false;
    for (std::uint64_t k = 0; k < tess::ShapeTraits<mv::Shape2D>::chunk_count; ++k) {
      if (chunk_changed[static_cast<std::size_t>(k)]) {
        scenario.world.mark_content_changed(tess::ChunkKey{k});
        any = true;
      }
    }
    if (any) {
      tess::mark_pathing_dirty(scenario.state);
    }
  }
};

struct SeedResult {
  mv::Outcome outcome;
  std::uint64_t digest = 0;
};

[[nodiscard]] SeedResult run_arm(mv::Family family, unsigned trial,
                                 bool priced) {
  auto scenario = mv::build_scenario(family, trial);
  const auto ranking = mv::route_attachment_ranking(*scenario);
  SeedResult out;
  if (priced) {
    Pricing pricing;
    out.outcome = mv::settle_with_pibt(
        *scenario, ranking, [&](mv::Scenario& s) { pricing(s); });
  } else {
    out.outcome = mv::settle_with_pibt(*scenario, ranking);
  }
  out.digest = outcome_digest(*scenario, out.outcome);
  return out;
}

}  // namespace

int main() {
  constexpr std::array<mv::Family, 7> kFamilies = {
      mv::Family::Warehouse,    mv::Family::Ring,
      mv::Family::Colony,       mv::Family::RandomSparse,
      mv::Family::RandomMedium, mv::Family::RandomDense,
      mv::Family::Adversarial,
  };
  std::vector<double> log_ratios;
  bool any_regression = false;
  for (const auto family : kFamilies) {
    long long fail_canonical = 0;
    long long fail_priced = 0;
    for (unsigned trial = 0; trial < mv::trial_count(family); ++trial) {
      const auto canonical = run_arm(family, trial, false);
      const auto priced = run_arm(family, trial, true);
      const auto priced_replay = run_arm(family, trial, true);
      CHECK(priced.digest == priced_replay.digest, "%s t%u replay",
            std::string(mv::family_name(family)).c_str(), trial);

      // Gate 1: identical per-seed terminal classification multiset.
      auto canon_sorted = canonical.outcome.categories;
      auto priced_sorted = priced.outcome.categories;
      std::sort(canon_sorted.begin(), canon_sorted.end());
      std::sort(priced_sorted.begin(), priced_sorted.end());
      if (canon_sorted != priced_sorted) {
        any_regression = true;
        std::printf(
            "DIVERGENCE %s t%u: canonical arr=%zu wed=%zu seal=%zu cen=%zu "
            "| priced arr=%zu wed=%zu seal=%zu cen=%zu\n",
            std::string(mv::family_name(family)).c_str(), trial,
            canonical.outcome.count(mv::Category::Arrived),
            canonical.outcome.count(mv::Category::Wedged),
            canonical.outcome.count(mv::Category::Sealed),
            canonical.outcome.count(mv::Category::Censored),
            priced.outcome.count(mv::Category::Arrived),
            priced.outcome.count(mv::Category::Wedged),
            priced.outcome.count(mv::Category::Sealed),
            priced.outcome.count(mv::Category::Censored));
      } else if (canonical.outcome.ticks > 0 && priced.outcome.ticks > 0) {
        log_ratios.push_back(
            std::log(static_cast<double>(priced.outcome.ticks) /
                     static_cast<double>(canonical.outcome.ticks)));
      }
      const auto residual = [](const mv::Outcome& o) {
        return static_cast<long long>(o.count(mv::Category::Wedged) +
                                      o.count(mv::Category::Sealed) +
                                      o.count(mv::Category::Censored));
      };
      fail_canonical += residual(canonical.outcome);
      fail_priced += residual(priced.outcome);
    }
    std::printf("c5 %-14s failures canonical=%lld priced=%lld%s\n",
                std::string(mv::family_name(family)).c_str(), fail_canonical,
                fail_priced,
                fail_priced > fail_canonical ? "  <-- WORSE" : "");
    if (fail_priced > fail_canonical) any_regression = true;
  }

  // Gate 4, scoped precisely: this scripted replay drives CONTENT
  // invalidation and replanning (mark_content_changed +
  // mark_pathing_dirty) with pricing active; no region graph or
  // precheck is attached in this harness, so chunk TOPOLOGY freshness
  // is deliberately out of scope here -- the demo-classifier leg covers
  // the full topology-invalidation composition through the colony
  // model's own set_wall channel, which rebuilds its graph while
  // pricing runs.
  {
    const auto pick_edit_tile = [](const mv::Scenario& s) {
      const auto extent = s.options.extent;
      for (int y = extent / 2; y < extent; ++y) {
        for (int x = extent / 2; x < extent; ++x) {
          if (mv::grid_at(s.terrain, extent, x, y)) {
            return tess::Coord3{x, y, 0};
          }
        }
      }
      return tess::Coord3{0, 0, 0};
    };
    const auto run_edited = [&](bool priced) {
      auto scenario = mv::build_scenario(mv::Family::Warehouse, 0);
      const auto ranking = mv::route_attachment_ranking(*scenario);
      const auto edit_tile = pick_edit_tile(*scenario);
      Pricing pricing;
      int tick = 0;
      const auto out = mv::settle_with_pibt(
          *scenario, ranking, [&](mv::Scenario& s) {
            if (tick == 32 || tick == 96) {
              const bool close = tick == 32;
              s.world.field<mv::PassableTag>(edit_tile) = !close;
              mv::grid_set(s.terrain, s.options.extent,
                           static_cast<int>(edit_tile.x),
                           static_cast<int>(edit_tile.y), !close);
              const auto key = tess::chunk_key<mv::Shape2D>(
                  tess::chunk_coord<mv::Shape2D>(edit_tile));
              s.world.mark_content_changed(key);
              tess::mark_pathing_dirty(s.state);
            }
            ++tick;
            if (priced) pricing(s);
          });
      return std::pair{out, outcome_digest(*scenario, out)};
    };
    const auto [c1, d1] = run_edited(true);
    const auto [c2, d2] = run_edited(true);
    CHECK(d1 == d2, "edit replay determinism");
    CHECK(c1.count(mv::Category::Censored) == 0, "edit replay censored");
    std::printf(
        "c5 edit-replay (priced): ticks=%d arrived=%zu wedged=%zu "
        "sealed=%zu\n",
        c1.ticks, c1.count(mv::Category::Arrived),
        c1.count(mv::Category::Wedged), c1.count(mv::Category::Sealed));
  }

  // Pooled paired ticks over classification-identical seeds + bootstrap.
  const auto geo = [](const std::vector<double>& logs) {
    double sum = 0;
    for (const auto v : logs) sum += v;
    return logs.empty() ? 1.0 : std::exp(sum / static_cast<double>(logs.size()));
  };
  mv::SplitMix64 rng(0xC5C5C5C5C5C5C5C5ULL);
  std::vector<double> boots;
  std::vector<double> sample(log_ratios.size());
  for (int rep = 0; rep < 2000; ++rep) {
    for (std::size_t i = 0; i < log_ratios.size(); ++i) {
      sample[i] = log_ratios[rng.below(log_ratios.size())];
    }
    boots.push_back(geo(sample));
  }
  std::sort(boots.begin(), boots.end());
  const auto gm = geo(log_ratios);
  const auto lo = boots[static_cast<std::size_t>(0.025 * 2000)];
  const auto hi = boots[static_cast<std::size_t>(0.975 * 2000) - 1];
  std::printf(
      "\npooled gm(priced/canonical ticks)=%.4f CI [%.4f, %.4f] over %zu "
      "identical-classification seeds\n",
      gm, lo, hi, log_ratios.size());
  const bool value = !any_regression && gm <= 0.95 && hi < 1.0;
  std::printf("substrate-leg verdict: %s\n",
              any_regression
                  ? "REJECT (classification regression / re-rejection rule)"
                  : (value ? "ACCEPT AS CALLER RECIPE" : "NEUTRAL, NO VALUE"));
  // The exit status and this line cover the MECHANICAL checks only
  // (replay determinism, edit-replay integrity); the parity verdict above
  // is the experiment's decision and prints independently, so a rejected
  // run does not end with a false success summary.
  std::printf("mechanical checks: %s (%d)\n",
              failures == 0 ? "clean" : "FAILED", failures);
  const bool experiment_clean = failures == 0 && !any_regression;
  return experiment_clean ? 0 : 1;
}
```
