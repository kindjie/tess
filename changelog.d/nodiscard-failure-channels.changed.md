- 106 public functions returning a status or result type are now
  `[[nodiscard]]`, closing the gap the audit named: 529 sites already had
  the attribute, so these were the exceptions rather than the rule.
  Because the library is exception-free, the returned value is the only
  failure channel — `load_world_archive(world, bytes);` as a bare
  statement silently ignored `Corrupt`. Source-breaking for callers that
  discard these values under `-Werror`, which is why it lands before 1.0
  rather than after.
- `build_region_graph` is deliberately excluded and now documents why. Its
  status is invariantly `Built`: the dense branch iterates keys
  `0..chunk_count` so `InvalidChunk` cannot arise and `MissingChunk` does
  not exist under `AlwaysResident`, and the sparse branch builds from
  `resident_chunk_keys()`, which are in-world and resident by
  construction. `update_region_graph` does have a reachable failure —
  `InvalidChunk` for an out-of-range dirty chunk — and is marked.
- `update_region_graph` discards the per-chunk build status in both of its
  incremental branches, and both are now written as deliberate with the
  argument that makes each sound. They differ: the dense branch relies on
  `MissingChunk` not existing under `AlwaysResident`, the sparse branch on
  the residency-generation check having already diverted every eviction to
  a full rebuild.
- `build_region_graph`, which cannot fail, returns `RegionGraphBuildResult`;
  `update_region_graph`, which can reject an invalid dirty chunk, returns the
  status-bearing `TopologyBuildResult`.
