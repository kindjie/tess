# tess_path_runtime_sparse_test

- `tess_path_runtime_sparse_test`: pins runtime, cache, movement, and render
  behavior over `SparseResidentWorld`. Evicting and reloading the same key at
  unchanged resident count must invalidate via `residency_generation` alone:
  its chunk version resets to zero, so a version-only fingerprint misses the
  replacement. Movement and rendering must never index non-resident storage;
  the suite also runs under ASan.
