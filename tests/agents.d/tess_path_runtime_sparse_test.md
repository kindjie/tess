# tess_path_runtime_sparse_test

- `tess_path_runtime_sparse_test`: verifies the path runtime over a
  `SparseResidentWorld` (Slice 5a): unit route-cache hits within the resident
  set with no spurious invalidation, an in-place edit invalidating via the
  version term, an evict+reload of the SAME key at unchanged resident count
  invalidating via `residency_generation` alone (the case a version-only
  fingerprint misses, since a reloaded chunk's version resets to 0), an
  eviction on a cached route re-planning to a non-Found result rather than
  serving stale, and `process_weighted_batch` routing across resident chunks
  (Found, never Indeterminate under the default policy) then reporting NoPath
  after a wall edit, and `validate_movement_intent` / `movement_versions_match`
  rejecting a move into or out of an evicted chunk (`InvalidFrom`/`InvalidTo`/
  `StaleVersion`) rather than reading a non-resident slot out of bounds, and
  `render_tile_deltas` / `clear_render_delta_dirty` scanning only the resident
  set (a dirty resident chunk contributes its deltas; a non-resident chunk is
  never scanned) — all run under ASan.
