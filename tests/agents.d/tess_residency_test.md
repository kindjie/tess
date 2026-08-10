# tess_residency_test

- `tess_residency_test`: verifies the byte-budgeted `SparseResidentWorld`:
  an enormous bounded shape (~3e10 chunks) constructs and stays empty until
  `ensure_resident`, which materializes a zeroed page and reports residency;
  out-of-bounds keys are never resident and are distinct from `Missing`
  in-bounds keys; `ensure_resident` is idempotent and preserves data; the
  byte budget caps resident bytes under least-recently-used eviction; an
  evicted chunk reloads with a strictly greater generation (invalidating any
  prior `ResidencyHandle`) and fresh zeroed data; explicit `evict` releases
  residency and bytes; `resident_chunk_keys` enumerates exactly the resident
  set; dirty/active queries visit only resident chunks (no full-world scan);
  directory coverage pins direct full-key-space lookup, erase, slot reuse, and
  out-of-range rejection plus bounded hashed lookup for large key spaces and
  backward-shift deletion under churn; both warm resident-set access and
  evict/reload slot reuse allocate nothing after warmup. Sparse path coverage
  also pins bounded weighted-field
  status precedence when one flood encounters both missing topology and cost
  overflow.
