# C4 measurement programs: recorded source

The mechanism itself merges as `tests/escalation_harness.h`; these are
the evidence programs that ran the full substrate sweep and the
per-seed diagnostics, with captured output alongside.

## c4_sweep.cc (full 132-seed plain-vs-armed sweep)

```cpp
#include <cstdio>
#include "escalation_harness.h"
namespace mv = tess_test::movement;
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
      auto as = mv::build_scenario(family, trial);
      const auto ar = mv::route_attachment_ranking(*as);
      mv::EscalationStats st;
      const auto a = mv::settle_with_pibt_escalation(*as, ar, &st);
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
}
```

## c4_seed_debug.cc (single-seed diagnostic used during the defect hunt)

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
