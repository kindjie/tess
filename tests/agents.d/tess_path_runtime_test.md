# tess_path_runtime_test

- `tess_path_runtime_test`: verifies the path request runtime MVP, including
  ticketed request/result lookup, stable copied result spans, unit route-cache
  reuse and invalidation across world edits, opt-in unit field-product cache
  reuse for repeated goals, start-chunk policy skip/use counters, stale product
  rejection, runtime cache clearing cadence, many-agent weighted batch
  processing through shared-goal fields, opt-in byte-budgeted weighted
  field-product reuse across processing calls with allocation-free warm
  replay, preflight rejection of unit and weighted products whose distance
  storage cannot fit the cache budget (avoiding a duplicate build and
  over-budget store),
  caller-configured cache clearing
  after repeated world edits, field-product-cache lookup-pointer stability
  across stores of other keys, and portal segment-cache runtime stats and
  `clear_caches()` for entries stored through the runtime accessor (the
  runtime's own processing passes do not populate that cache). It also
  covers runtime lifecycle: `clear_requests()` starting a fresh frame with
  regenerated tickets, empty request lists processing to empty results,
  failure stat tallies (invalid start/goal, no path) over mixed unit and
  weighted batches, zero-cost weighted-start rejection under product reuse,
  and the shared policy byte budget driving real field-product eviction
  through `process_unit_cached`. Seeded (`std::mt19937`, fixed
  seeds) randomized equivalence pins repeated-goal grouping against a
  per-request A* oracle — statuses, costs, and the candidate/used/skipped
  group counters versus a reference computation — across both start-chunk
  policies. An out-of-shape start is resolved inside the grouping pass before
  unchecked tile-key conversion, and a warm identical frame is allocation-
  free.
