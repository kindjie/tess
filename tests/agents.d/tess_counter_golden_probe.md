# tess_counter_golden_probe

- `tess_counter_golden_probe`: gtest-free producer for the shadow-mode path
  and queued-phase counter goldens. Fixed serial workloads pin their functional
  outcomes before emitting counter JSON; counter drift remains advisory until
  the strict promotion.
