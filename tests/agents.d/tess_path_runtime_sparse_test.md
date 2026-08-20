# tess_path_runtime_sparse_test

- `tess_path_runtime_sparse_test`: pins runtime, cache, movement, and render
  behavior over `SparseResidentWorld`. Evicting and rematerializing the same key at
  unchanged resident count must invalidate via `residency_generation` alone:
  its chunk content version resets to zero, so a version-only fingerprint
  misses the replacement. Movement and rendering must never index
  non-resident storage. `ReportIndeterminate` is the public search, field,
  cache, batch, runtime, and agent default; explicit `AssumeImpassable`
  produces a policy-relative `NoPath`; prechecks preserve either choice; and
  policy changes invalidate negative cache entries. Shared weighted fields
  keep already reached members `Found` while independently classifying
  unreached members. The suite also runs under ASan.
