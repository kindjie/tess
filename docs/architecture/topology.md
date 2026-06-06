# Topology Foundation

The current topology layer is a local chunk-region foundation. It lives under
`include/tess/topology/` and is exported by `tess/tess.h`.

## Public Surface

- `LocalRegionId` identifies one passable connected component inside one
  chunk. `invalid_local_region` is used for blocked tiles and invalid lookups.
- `LocalRegion` summarizes one local region with tile count, world-space
  bounds, and boundary-exit count.
- `LocalBoundaryExit` records one passable local boundary tile that has an
  adjacent resident chunk in the compile-time shape.
- `LocalChunkTopology` owns local region labels, region summaries, boundary
  exits, the chunk key, and the captured chunk topology version.
- `LocalTopologyScratch` owns reusable flood-fill stack storage.
- `build_local_chunk_topology<World, PassableTag>(world, chunk, scratch,
  topology)` labels passable connected components for one chunk and records
  boundary exits.

## Behavior

Local topology uses six axis-adjacent movement inside one chunk:

```text
+x, -x, +y, -y, +z, -z
```

Degenerate axes naturally have no local neighbor candidates. Boundary exits
are emitted only when the passable boundary tile has a neighboring chunk inside
the compile-time shape, so single-chunk and degenerate-axis worlds do not
create synthetic exits.

The builder treats the passability field as boolean-like. Blocked tiles keep
`invalid_local_region`. Region IDs are assigned deterministically in increasing
local tile order, then depth-first flood fill order. The result captures
`world.meta(chunk).topology_version`; this slice does not mutate dirty flags or
metadata versions.

## Deliberate Limits

This slice does not build an inter-chunk portal graph, movement-class rule DSL,
special transitions, dirty rebuild queue, missing-chunk policy, or pathfinding
precheck. Boundary exits are local facts that a later portal graph can pair
with neighboring chunk topology.
