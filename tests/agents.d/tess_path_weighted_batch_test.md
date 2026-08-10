# tess_path_weighted_batch_test

- `tess_path_weighted_batch_test`: verifies `weighted_path_batch` edges:
  empty batches, all-distinct goals (pure A* fallbacks, no field builds),
  duplicate identical requests sharing one field build, per-member statuses
  for failed shared-goal groups matching `weighted_astar_path`'s endpoint
  validation precedence (invalid starts are not mislabeled with the goal's
  failure status), cross-builder scratch reuse from provider heap fields to
  plain bounded fields sizing settle-target epochs, realized bucket overflow
  returning directly to per-member
  A* without a second full flood and withdrawing the partial field's replay
  stamp, >MaxCost corridor tiles engaging the
  unbounded fallback
  (exact costs plus bounded-vs-unbounded build equality), seeded random-cost
  bounded/unbounded field equivalence, seeded batch-vs-oracle equivalence
  including the grouping stats counters, and allocation-free warm repeat
  batches.
