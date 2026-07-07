# Topology Foundation

The current topology layer is a local chunk-region foundation. It lives under
`include/tess/topology/` and is exported by `tess/tess.h`.

## Public Surface

- `LocalRegionId` identifies one passable connected component inside one
  chunk. Ids are 1-based: `invalid_local_region` (value 0) is used for
  blocked tiles and invalid lookups, and id N maps to `regions()[N - 1]`.
- `LocalRegion` summarizes one local region with tile count, world-space
  bounds, and boundary-exit count.
- `LocalBoundaryExit` records one passable local boundary tile that has an
  adjacent resident chunk in the compile-time shape.
- `LocalChunkTopology` owns local region labels, region summaries, boundary
  exits, the chunk key, and the captured chunk topology version.
  `region(LocalRegionId)` is the checked accessor for the 1-based id
  convention; it returns `nullptr` for invalid or out-of-range ids.
- `LocalTopologyScratch` owns reusable flood-fill stack storage.
- `RegionRef` identifies a local region in a specific chunk.
- `RegionPortal` records one directed passable transition between neighboring
  chunk-local regions.
- `RegionGraph` owns all local chunk topologies, paired directed portals for
  an always-resident world, and a dense global region index with a CSR
  portal adjacency. `region_count()` reports the dense index size and
  `region_index(RegionRef)` maps a region reference to its dense index
  (`invalid_region_index` for invalid or out-of-range references).
- `RegionGraphScratch` owns reusable reachability traversal storage with
  epoch-stamped visited marks.
- `build_local_chunk_topology<World, PassableTag>(world, chunk, scratch,
  topology)` labels passable connected components for one chunk and records
  boundary exits.
- `build_region_graph<World, PassableTag>(world, scratch, graph)` rebuilds
  local topology for every chunk, pairs boundary exits whose neighbor tile is
  passable, and rebuilds the dense region index and CSR adjacency.
- `update_region_graph<World, PassableTag>(world, scratch, graph,
  dirty_chunks)` incrementally patches a built graph after passability edits
  confined to the dirty chunks and returns the same aggregate result a full
  rebuild would.
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
neighboring chunk, a directed `RegionPortal` is emitted. Portals are stored
in canonical build order: ascending from-chunk, then boundary-exit order
within the chunk. After pairing, the builder assigns every region a dense
global index (per-chunk prefix sums over 1-based local ids) and fills a CSR
adjacency over portals, preserving portal order within each from-region
bucket for deterministic traversal.

Reachability first maps endpoints to local regions, rejects blocked or
out-of-shape endpoints, and then runs the traversal over the CSR adjacency
using dense region indices and epoch-stamped visited marks, which makes one
query O(regions + portals) instead of rescanning the portal array per
frontier pop. Visited-region counts are unchanged from the portal-scan
implementation because the CSR buckets preserve portal order.

`update_region_graph` patches a built graph in place. It re-runs
`build_local_chunk_topology` for each dirty chunk, drops every portal
originating from a dirty chunk or one of its face neighbors in one filtered
pass, re-derives those chunks' portals from their boundary exits, and
stable-sorts portals by from-chunk to restore canonical build order before
rebuilding the dense index and CSR adjacency. The result is identical to a
fresh `build_region_graph` over the edited world, including portal order.
An empty dirty set is a no-op; a dirty set covering all chunks is
equivalent to a full build. Passing a graph that was never built for the
world shape falls back to a full build, and an out-of-range dirty chunk is
rejected with `InvalidChunk` before any mutation.

## Deliberate Limits

This slice does not implement movement-class rule DSL, special transitions,
dirty rebuild queue, missing-chunk policy, hierarchical/coarse paths, or
pathfinding precheck integration. The portal graph stores directed
tile-adjacent portals only; incremental updates require the caller to supply
the dirty chunk set.
