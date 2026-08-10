# tess_storage_test

- `tess_storage_test`: verifies typed field schemas, resident chunk pages, and
  always-resident dense worlds, including SoA field independence, contiguous
  typed spans, metadata, const access, key/coord lookup, coordinate resolution,
  checked invalid-coordinate behavior, per-chunk dirty/active/topology-version
  metadata, dirty-bounds union across all relative box orientations including
  z cases, zero-initialized fresh-world field values without prior writes,
  appending out-parameter dirty/active chunk queries that match the by-value
  queries and do not allocate into reserved vectors, generation-stamped
  `observe_dirty`/`clear_dirty_observed` maintenance clears that preserve
  marks landing after observation, noexcept hot accessors, and
  allocation-free local field/span/world access after construction.
