# tess_topology_movement_test

- `tess_topology_movement_test`: verifies per-class region labeling and the
  graph movement-class stamp (S5.3): the `WalkableField` identity build is
  byte-identical to the legacy raw-tag build (labels, regions, exits, portals,
  index), Walker/Builder labels diverge exactly on construction tiles (with
  the Builder-only region bridge across a wall of sites), per-class
  incremental `update_region_graph` equals a full rebuild, a class-stamp
  mismatch forces a full rebuild even with an empty dirty set,
  `is_region_graph_fresh_for` is per-class (raw tag shares the identity
  class's stamp; unbuilt graphs match no class), a warm per-class relabel
  of one chunk is allocation-free, and the transition-provider contract
  (S5.7): the default `AdjacentTransitions` build is identical to the
  providerless build, a bridge provider's directed portals connect walled
  regions (both directions), incremental update equals a full rebuild with
  the same provider, a provider-type, live-instance, or provider-revision
  mismatch forces a full rebuild, runtime precheck ignores a graph stamped for
  another equal-revision provider instance, stateful providers without a
  revision are rejected, and a sparse provider transition into a non-resident
  chunk degrades reachability to `Indeterminate` instead of a wrong
  `Unreachable`; and the stair provider
  (S5.8): an offset stair links two z-levels with no vertical face adjacency
  in both directions (cross-chunk and same-chunk landings), stair edges are
  per-class (a construction-site landing is Builder-only), incremental
  update equals a full rebuild across stair add/remove, a stair whose
  landing would cross two chunk boundaries at once contributes nothing, a
  sideways-crossing landing (x/y chunk boundary at a local z below the
  chunk top) emits BOTH directions with incremental equality across the
  seam, an out-of-range stair field value reads as `None` (including wide
  fields whose values would wrap when narrowed), and `WalkableCostField`
  labels a graph without the span fast path (zero-cost tiles unlabeled).
