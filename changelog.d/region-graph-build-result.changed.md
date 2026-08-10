- `build_region_graph` returns a new `RegionGraphBuildResult` instead of
  the shared `LocalTopologyResult`. It carries the same counts without a
  status, because that build cannot fail: the dense branch iterates keys
  `0..chunk_count`, so `InvalidChunk` cannot arise and `MissingChunk` does
  not exist under `AlwaysResident`, and the sparse branch builds from
  `resident_chunk_keys()`, which are in-world and resident by
  construction. `build_local_chunk_topology` and `update_region_graph`
  keep `LocalTopologyResult`; the latter's `InvalidChunk` for an
  out-of-range dirty chunk is reachable.
- This is the same dead-status-channel defect as `save_world_archive`'s,
  and the type split is the same remedy. The measurable difference is what
  it removed: **45 assertions across six test files** compared a status
  that was invariantly `Built`. The type now makes them compile errors
  rather than relying on anyone to notice.
- The two branches that propagated an impossible status now `fail_fast`
  instead. If either ever fires, the residency assumptions the function
  rests on have changed, and continuing would publish a half-built graph.
- An update that falls back to a full rebuild converts the result,
  reporting `Built`.
