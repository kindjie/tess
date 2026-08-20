# tess_path_agent_test

- `tess_path_agent_test`: pins the public path-agent lifecycle, movement
  failure classification, provider-aware validation, and unit and weighted
  batching. The optional `last_result` is absent before search and after route
  invalidation; `NoPath` is stored only when a search returns it. Provider
  exceptions must leave occupancy, reservations, and dirty
  metadata unchanged. Warm no-allocation cases also assert submitted/found
  statistics, so a skipped frame cannot pass vacuously.
