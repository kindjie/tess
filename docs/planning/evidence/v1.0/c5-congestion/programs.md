# C5 measurement program: recorded source

One binary, both arms interleaved, compiled against the C0 substrate
on main `b87900a5`. Captured output is `arms.txt` alongside.

## c5_arms.cc

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

  // Gate 4: scripted edit replay with pricing active (warehouse trial 0;
  // close then reopen one free tile at ticks 32 and 96 through the
  // versioned channel). The gate is that pricing composes with topology
  // invalidation: deterministic replay, no censoring, and a terminal
  // classification recorded rather than asserted.
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
  std::printf("verdict: %s\n",
              any_regression
                  ? "REJECT (classification regression / re-rejection rule)"
                  : (value ? "ACCEPT AS CALLER RECIPE" : "NEUTRAL, NO VALUE"));
  std::printf("%s\n", failures == 0 ? "ALL GATES PASSED" : "GATE FAILURES");
  return failures == 0 ? 0 : 1;
}
```
