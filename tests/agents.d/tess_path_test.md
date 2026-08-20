# tess_path_test

- `tess_path_test`: broad foundation coverage for unit and weighted A*, route,
  segment and field-product caches, distance fields, products, replay, and
  batching across supported shapes. Cache-hit paths are copied into caller
  scratch, so later cache growth cannot invalidate returned spans. Store and
  compaction failures preserve existing entries, misses leave caller storage
  untouched, and bounded-field overflow falls back per request. Repeated
  queries with reserved scratch allocate nothing. Seeded weighted-search
  tie-breaking is deterministic, preserves optimal cost, and can select both
  sides of a symmetric equal-cost barrier. Default, failed, stale, and
  model-mismatched products report `NotComputed`, while an authoritative
  exhausted search reports `NoPath`.
