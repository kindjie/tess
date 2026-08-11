# tess_path_runtime_test

- `tess_path_runtime_test`: pins ticket, lifecycle, cache, batching, policy,
  failure-stat, and allocation contracts for the path request runtime. Seeded
  repeated-goal grouping is checked against per-request A* and an independent
  computation of every grouping counter. Oversized field products are rejected
  before a duplicate build, and out-of-shape starts before unchecked key
  conversion. Stable result and lookup spans remain valid across unrelated
  cache growth; an identical warm frame allocates nothing.
