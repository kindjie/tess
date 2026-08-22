# C1 measurement programs

Recorded as source rather than as build inputs, matching the precedent set
by the P1 seam stand-in: the repository's C++ tooling governs tracked `.cc`
files, and these are evidence, not code the project builds.

To reproduce, copy a block into a `.cc` file and build it against the
worktree:

```sh
clang++ -std=c++20 -O2 -I include -I tests \
  -I build/dev/generated/include <program>.cc -o /tmp/<program>
```

## `decide`

The decision: per-family agent-level classification regressions and improvements, seed exclusions against the pre-registered 20% cap, and the pooled paired geometric mean of ticks-to-settle.

```cpp
#include "hindrance_ranking.h"
#include "movement_scenarios.h"
#include <cstdio>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>
namespace mv = tess_test::movement;

static auto run_control(mv::Scenario& s) -> mv::Outcome {
  auto r = mv::route_attachment_ranking(s);
  return mv::settle_with_pibt(s, r);
}
static auto run_candidate(mv::Scenario& s) -> mv::Outcome {
  mv::HindranceRanking r{mv::route_attachment_ranking(s),
                         std::span<const tess::PathAgentState>(s.agents),
                         &s.state.routes, true};
  return mv::settle_with_pibt(s, r, [&r](mv::Scenario&) { r.begin_pass(); });
}
// Lower is better. Arrived < {Wedged, GoalOccupied} < Sealed < Censored.
static int severity(mv::Category c) {
  switch (c) {
    case mv::Category::Arrived: return 0;
    case mv::Category::Wedged: return 1;
    case mv::Category::GoalOccupied: return 1;
    case mv::Category::Sealed: return 2;
    case mv::Category::Censored: return 3;
  }
  return 3;
}

int main() {
  std::vector<double> all_ratios;
  int total_regressions = 0, total_improvements = 0;
  int total_paired = 0, total_excluded = 0, total_seeds = 0;
  for (auto fam : {mv::Family::Warehouse, mv::Family::Ring, mv::Family::Colony,
                   mv::Family::RandomSparse, mv::Family::RandomMedium,
                   mv::Family::RandomDense, mv::Family::Adversarial}) {
    int reg = 0, imp = 0, paired = 0, excl = 0;
    for (unsigned t = 0; t < mv::trial_count(fam); ++t) {
      auto a = mv::build_scenario(fam, t);
      auto b = mv::build_scenario(fam, t);
      auto oc = run_control(*a);
      auto oh = run_candidate(*b);
      ++total_seeds;
      bool changed = false;
      for (std::size_t i = 0; i < oc.categories.size(); ++i) {
        const int d = severity(oh.categories[i]) - severity(oc.categories[i]);
        if (d > 0) { ++reg; changed = true; }
        else if (d < 0) { ++imp; changed = true; }
      }
      if (changed) { ++excl; continue; }
      all_ratios.push_back(double(oh.ticks) / double(oc.ticks));
      ++paired;
    }
    std::printf("%-14s seeds=%2u paired=%2d excluded=%2d (%.0f%%)  "
                "agent regressions=%d improvements=%d\n",
                std::string(mv::family_name(fam)).c_str(),
                mv::trial_count(fam), paired, excl,
                100.0 * excl / mv::trial_count(fam), reg, imp);
    total_regressions += reg; total_improvements += imp;
    total_paired += paired; total_excluded += excl;
  }
  double ls = 0; for (double r : all_ratios) ls += std::log(r);
  const double geo = all_ratios.empty() ? 1.0 : std::exp(ls / all_ratios.size());
  std::printf("\nPOOLED paired=%d excluded=%d of %d seeds (%.0f%%)  "
              "geomean=%.4f (%+.2f%%)\n", total_paired, total_excluded,
              total_seeds, 100.0 * total_excluded / total_seeds, geo,
              (geo - 1.0) * 100.0);
  std::printf("AGENT-LEVEL regressions=%d improvements=%d\n",
              total_regressions, total_improvements);
}
```

## `engagement`

The check that the null is not vacuous: counts ranking calls whose hindrance term is nonzero, per family.

```cpp
#include "hindrance_ranking.h"
#include "movement_scenarios.h"
#include <cstdio>
#include <string>
namespace mv = tess_test::movement;

// Counts how often the tie-break could actually change an outcome:
// a ranking call whose hindrance term is nonzero.
struct CountingRanking {
  mv::HindranceRanking inner;
  mutable long calls = 0;
  mutable long nonzero_h = 0;
  auto operator()(std::size_t agent, tess::Coord3 c) const -> std::uint32_t {
    ++calls;
    const auto base = inner.base(agent, c);
    const auto composed = inner(agent, c);
    constexpr auto kD = tess::RouteAttachmentRanking::kDetachedBase;
    const auto h = base < kD ? composed - base * mv::HindranceRanking::kScale
                             : composed - (kD + (base - kD) *
                                                    mv::HindranceRanking::kScale);
    if (h != 0) ++nonzero_h;
    return composed;
  }
  void begin_pass() const { inner.begin_pass(); }
};

int main() {
  for (auto fam : {mv::Family::Warehouse, mv::Family::Ring, mv::Family::Colony,
                   mv::Family::RandomDense, mv::Family::Adversarial}) {
    long calls = 0, nz = 0;
    for (unsigned t = 0; t < mv::trial_count(fam); ++t) {
      auto s = mv::build_scenario(fam, t);
      CountingRanking r{{mv::route_attachment_ranking(*s),
                         std::span<const tess::PathAgentState>(s->agents),
                         &s->state.routes, true}};
      (void)mv::settle_with_pibt(*s, r, [&r](mv::Scenario&) { r.begin_pass(); });
      calls += r.calls; nz += r.nonzero_h;
    }
    std::printf("%-14s ranking calls=%-9ld with nonzero hindrance=%-8ld (%.2f%%)\n",
                std::string(mv::family_name(fam)).c_str(), calls, nz,
                calls ? 100.0 * nz / calls : 0.0);
  }
}
```

## `per_family`

Per-family tick totals, geometric means, and wedged/sealed counts for both arms.

```cpp
#include "hindrance_ranking.h"
#include "movement_scenarios.h"
#include <cstdio>
#include <cmath>
#include <string>
namespace mv = tess_test::movement;

static auto run_control(mv::Scenario& s) -> mv::Outcome {
  auto r = mv::route_attachment_ranking(s);
  return mv::settle_with_pibt(s, r);
}
static auto run_candidate(mv::Scenario& s) -> mv::Outcome {
  mv::HindranceRanking r{mv::route_attachment_ranking(s),
                         std::span<const tess::PathAgentState>(s.agents),
                         &s.state.routes, true};
  return mv::settle_with_pibt(s, r, [&r](mv::Scenario&) { r.begin_pass(); });
}

int main() {
  for (auto fam : {mv::Family::Warehouse, mv::Family::Ring, mv::Family::Colony,
                   mv::Family::RandomSparse, mv::Family::RandomMedium,
                   mv::Family::RandomDense, mv::Family::Adversarial}) {
    double log_sum = 0; int paired = 0, excluded = 0;
    long cticks = 0, hticks = 0;
    std::size_t cw = 0, hw = 0, cs = 0, hs = 0;
    for (unsigned t = 0; t < mv::trial_count(fam); ++t) {
      auto a = mv::build_scenario(fam, t);
      auto b = mv::build_scenario(fam, t);
      auto oc = run_control(*a);
      auto oh = run_candidate(*b);
      cw += oc.count(mv::Category::Wedged); hw += oh.count(mv::Category::Wedged);
      cs += oc.count(mv::Category::Sealed);  hs += oh.count(mv::Category::Sealed);
      if (oc.categories != oh.categories) { ++excluded; continue; }
      if (oc.ticks <= 0 || oh.ticks <= 0) { ++excluded; continue; }
      cticks += oc.ticks; hticks += oh.ticks;
      log_sum += std::log(double(oh.ticks) / double(oc.ticks));
      ++paired;
    }
    const double geo = paired ? std::exp(log_sum / paired) : 1.0;
    std::printf("%-14s paired=%2d excluded=%2d  ticks ctl=%5ld cand=%5ld  "
                "geomean=%.4f (%+.2f%%)  wedged %zu->%zu  sealed %zu->%zu\n",
                std::string(mv::family_name(fam)).c_str(), paired, excluded,
                cticks, hticks, geo, (geo - 1.0) * 100.0, cw, hw, cs, hs);
  }
}
```
