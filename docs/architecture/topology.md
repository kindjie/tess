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
- `RegionRef` identifies a local region in a specific chunk.
- `RegionPortal` records one directed passable transition between neighboring
  chunk-local regions.
- `RegionGraph` owns all local chunk topologies and paired directed portals
  for an always-resident world.
- `RegionGraphScratch` owns reusable reachability traversal storage.
- `build_local_chunk_topology<World, PassableTag>(world, chunk, scratch,
  topology)` labels passable connected components for one chunk and records
  boundary exits.
- `build_region_graph<World, PassableTag>(world, scratch, graph)` rebuilds
  local topology for every chunk and pairs boundary exits whose neighbor tile is
  passable.
- `reachable<Shape>(graph, start, goal, scratch)` checks whether two
  coordinates are connected through local regions and paired portals.

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

`RegionGraph` pairs exits by looking at the adjacent world coordinate across
each boundary exit. If that tile belongs to a passable local region in the
neighboring chunk, a directed `RegionPortal` is emitted. Reachability first
maps endpoints to local regions, rejects blocked or out-of-shape endpoints, and
then performs a graph traversal over portals.

## Deliberate Limits

This slice does not implement movement-class rule DSL, special transitions,
dirty rebuild queue, missing-chunk policy, hierarchical/coarse paths, or
pathfinding precheck integration. The portal graph is rebuilt from all resident
chunks and stores directed tile-adjacent portals only.
