# tess_chunk_maintenance_test

- `tess_chunk_maintenance_test`: pins the experimental external chunk
  adapter's dense and sparse slot bindings, dirty-mask ownership,
  generation-safe product tokens, explicit quiescent residency transitions,
  capacity/retry/exception behavior, deterministic backend equivalence,
  canonical archive independence, warmed allocation behavior, and release
  lifetime. Sparse residency changes remain adapter-exclusive after binding;
  producer/drain concurrency is exercised only where world access is
  externally synchronized or scheduling does not touch the world.
