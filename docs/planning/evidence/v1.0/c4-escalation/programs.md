# C4 measurement programs: recorded source

The mechanism itself merges as `tests/escalation_harness.h`; these are
the evidence programs that ran the full substrate sweep and the
per-seed diagnostics, with captured output alongside.

## c4_sweep.cc (full 132-seed plain-vs-armed sweep)

Strengthened in the pre-RC audit: the original version printed
observations and always exited zero, so the record's determinism and
aggregate claims were captured but unenforced. Each arm now settles
twice with per-seed outcome-digest comparison, the recorded aggregates
are asserted exactly, and the exit status reflects every check.

```cpp
// Strengthened per the pre-RC audit: each arm now settles TWICE with a
// per-seed outcome-digest comparison (the determinism the record
// claims), the recorded aggregates are asserted as exact expectations,
// and the exit status reflects every check -- the original version
// printed observations and always succeeded.
#include <cstdio>
#include "escalation_harness.h"
namespace mv = tess_test::movement;
namespace {
int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures;   std::printf("CHECK FAIL line %d: %s ", __LINE__, #cond);   std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)
[[nodiscard]] std::uint64_t outcome_digest(const mv::Scenario& s,
                                           const mv::Outcome& o) {
  std::uint64_t h = 0xCBF29CE484222325ULL;
  const auto mix = [&h](std::uint64_t v) {
    h ^= v + 0x9E3779B97F4A7C15ULL + (h << 6U) + (h >> 2U);
  };
  for (const auto& a : s.agents) {
    mix((std::uint64_t)a.position.x); mix((std::uint64_t)a.position.y);
  }
  for (const auto c : o.categories) mix((std::uint64_t)c);
  mix((std::uint64_t)o.ticks);
  return h;
}
}  // namespace
int main() {
  constexpr std::array<mv::Family, 7> kFamilies = {
      mv::Family::Warehouse, mv::Family::Ring, mv::Family::Colony,
      mv::Family::RandomSparse, mv::Family::RandomMedium,
      mv::Family::RandomDense, mv::Family::Adversarial};
  int clean=0, clean_dirty=0;
  int res=0, res_same=0, res_better=0, res_worse=0, res_mixed=0;
  long long d_arr=0, d_wed=0, d_seal=0;
  unsigned long long fires=0;
  for (const auto family : kFamilies) {
    for (unsigned trial = 0; trial < mv::trial_count(family); ++trial) {
      auto ps = mv::build_scenario(family, trial);
      const auto pr = mv::route_attachment_ranking(*ps);
      const auto p = mv::settle_with_pibt(*ps, pr);
      auto ps2 = mv::build_scenario(family, trial);
      const auto pr2 = mv::route_attachment_ranking(*ps2);
      const auto p2 = mv::settle_with_pibt(*ps2, pr2);
      CHECK(outcome_digest(*ps, p) == outcome_digest(*ps2, p2),
            "%s t%u plain replay",
            std::string(mv::family_name(family)).c_str(), trial);
      auto as = mv::build_scenario(family, trial);
      const auto ar = mv::route_attachment_ranking(*as);
      mv::EscalationStats st;
      const auto a = mv::settle_with_pibt_escalation(*as, ar, &st);
      auto as2 = mv::build_scenario(family, trial);
      const auto ar2 = mv::route_attachment_ranking(*as2);
      mv::EscalationStats st2;
      const auto a2 = mv::settle_with_pibt_escalation(*as2, ar2, &st2);
      CHECK(outcome_digest(*as, a) == outcome_digest(*as2, a2) &&
                st.fired == st2.fired,
            "%s t%u armed replay",
            std::string(mv::family_name(family)).c_str(), trial);
      const bool pclean = p.count(mv::Category::Arrived)==p.categories.size();
      if (pclean) { ++clean; if (st.fired) ++clean_dirty; continue; }
      ++res; fires += st.fired;
      const long long da = (long long)a.count(mv::Category::Arrived)-(long long)p.count(mv::Category::Arrived);
      const long long dw = (long long)a.count(mv::Category::Wedged)-(long long)p.count(mv::Category::Wedged);
      const long long dsl = (long long)a.count(mv::Category::Sealed)-(long long)p.count(mv::Category::Sealed);
      d_arr+=da; d_wed+=dw; d_seal+=dsl;
      if (da==0&&dw==0&&dsl==0) ++res_same;
      else if (da>0&&dw<=0&&dsl<=0) ++res_better;
      else if (da<0) ++res_worse; else ++res_mixed;
      if (da!=0||dw!=0||dsl!=0)
        std::printf("%s t%u: dArr=%+lld dWed=%+lld dSeal=%+lld fired=%llu\n",
          std::string(mv::family_name(family)).c_str(), trial, da, dw, dsl,
          (unsigned long long)st.fired);
    }
  }
  std::printf("\nclean=%d (fires on clean=%d) residual=%d same=%d better=%d worse=%d mixed=%d\n",
    clean, clean_dirty, res, res_same, res_better, res_worse, res_mixed);
  std::printf("residual totals: dArrived=%+lld dWedged=%+lld dSealed=%+lld fires=%llu\n",
    d_arr, d_wed, d_seal, fires);
  // The recorded aggregates, asserted: any drift from the retained
  // evidence fails the program instead of silently printing new truth.
  CHECK(clean == 71 && clean_dirty == 0, "clean population");
  CHECK(res == 61 && res_same == 56 && res_better == 3 &&
            res_worse == 1 && res_mixed == 1,
        "residual classification");
  CHECK(d_arr == 3 && d_wed == -2 && d_seal == -1 && fires == 91,
        "residual aggregate deltas");
  std::printf("sweep checks: %s (%d)\n",
              failures == 0 ? "ALL ASSERTED" : "FAILED", failures);
  return failures == 0 ? 0 : 1;
}
```

## c4_seed_debug.cc (single-seed diagnostic used during the defect hunt)

NOTE: this diagnostic's quoted outputs in `README.md` (the 18-fire /
16-abort collapse; the fired=3 one-seal case) were taken against
PRE-FIX revisions of the harness while the defects they diagnose still
existed. Compiling this source against the merged harness reproduces
the FIXED behavior, not those historical numbers -- deliberately: the
narrative is the history, the sweep output is the current truth.

```cpp
#include <cstdio>
#include "escalation_harness.h"
namespace mv = tess_test::movement;
int main() {
  auto plain_s = mv::build_scenario(mv::Family::Warehouse, 10);
  const auto pr = mv::route_attachment_ranking(*plain_s);
  const auto plain = mv::settle_with_pibt(*plain_s, pr);
  auto armed_s = mv::build_scenario(mv::Family::Warehouse, 10);
  const auto ar = mv::route_attachment_ranking(*armed_s);
  mv::EscalationStats stats;
  const auto armed = mv::settle_with_pibt_escalation(*armed_s, ar, &stats);
  std::printf("plain ticks=%d arrived=%zu wedged=%zu sealed=%zu | armed ticks=%d arrived=%zu wedged=%zu sealed=%zu\n",
    plain.ticks, plain.count(mv::Category::Arrived), plain.count(mv::Category::Wedged), plain.count(mv::Category::Sealed),
    armed.ticks, armed.count(mv::Category::Arrived), armed.count(mv::Category::Wedged), armed.count(mv::Category::Sealed));
  std::printf("stats fired=%llu skipb=%llu skipu=%llu aborted=%llu steps=%llu states=%llu\n",
    (unsigned long long)stats.fired,(unsigned long long)stats.skipped_bounds,(unsigned long long)stats.skipped_unsolvable,
    (unsigned long long)stats.aborted,(unsigned long long)stats.plan_steps_executed,(unsigned long long)stats.solver_states);
  for (std::size_t i=0;i<plain.categories.size();++i) {
    if (plain.categories[i]!=armed.categories[i]) {
      std::printf("  agent %zu: plain=%s armed=%s (armed pos %lld,%lld goal %lld,%lld)\n", i,
        std::string(mv::category_name(plain.categories[i])).c_str(),
        std::string(mv::category_name(armed.categories[i])).c_str(),
        (long long)armed_s->agents[i].position.x,(long long)armed_s->agents[i].position.y,
        (long long)armed_s->agents[i].goal.x,(long long)armed_s->agents[i].goal.y);
    }
  }
}
```
