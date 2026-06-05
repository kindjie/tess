# Path Foundation

The current path layer is a minimal always-resident A* foundation. It lives in
`include/tess/path/path.h` and is exported by `tess/tess.h`.

## Public Surface

- `PathRequest` contains a start and goal `Coord3`.
- `PathStatus` reports `Found`, `InvalidStart`, `InvalidGoal`, or `NoPath`.
- `PathResult` returns the status, unit movement cost, expanded-node count,
  reached-node count, and a non-owning span of path coordinates.
- `PathScratch` owns reusable vectors for open nodes, visited records, and the
  returned path. `reserve_nodes(count)` prepares storage for allocation-free
  repeated queries when capacity is sufficient.
- `astar_path<World, PassableTag>(world, request, scratch)` runs deterministic
  pathfinding over the existing always-resident world storage. The passability
  field is treated as boolean-like.

## Behavior

Movement uses six axis-adjacent candidates in fixed order:

```text
+x, -x, +y, -y, +z, -z
```

Candidates outside the compile-time shape are rejected through the existing
shape containment helpers. Degenerate axes naturally reject out-of-bounds
neighbors, so the same implementation covers top-down 2D, vertical 2D, and
small 3D worlds.

Costs are unit-weighted. The heuristic is Manhattan distance. Tie-breaking is
deterministic by lower total score, then higher path cost for equal-score
nodes, then tile-key order. Preferring higher path cost on equal-score nodes
avoids open-grid wavefront expansion while preserving shortest paths.

Before entering heap-backed A*, the implementation probes direct Manhattan
paths in the shape-relevant axis orders. If any route is fully passable, it
returns that direct shortest path immediately. If a direct probe hits a blocked
tile whose axis plane is fully blocked, it returns `NoPath` immediately because
the plane separates start from goal under the current axis-adjacent movement
model. For axis-aligned requests, a clear one-tile parallel detour is also
returned before A* because its Manhattan+2 cost is optimal when the straight
line is blocked. For top-down 2D requests blocked by a non-separating axis
plane, the implementation can scan that plane for the cheapest passable gap and
return a verified Manhattan route through it; the same logic applies to
vertical 2D layouts. It also handles 2D forced-gap sequences by walking toward
the goal, scanning a barrier line only when the next progress step is blocked,
and accepting only fully open lines or lines with exactly one passable gap. In
3D, a blocked direct route can scan the blocked axis plane for the cheapest
passable crossing and return a verified Manhattan route through it. Other
non-separating blockers fall back to normal A*.

The returned path span points into the supplied `PathScratch` and remains valid
until the next path query or scratch clear/reserve operation. Scratch keeps
dense per-tile state arrays and clears only nodes touched by the previous
query, so repeated queries avoid full-world scratch resets when the search
visits a small fraction of the world.

## Deliberate Limits

This MVP slice does not implement movement classes, weighted terrain, topology
prechecks, portal graphs, sparse residency, reservations, dynamic blockers,
distance fields, path caching, async tickets, or rich path diagnostics. The
implementation uses reusable dense per-tile scratch arrays and a heap-backed
open set; it is still an MVP path core, not the final topology-aware path
system.

This API is suitable for individual point-to-point queries and regression
coverage. It is not the intended scaling mechanism for hundreds of agents with
shared goals; the v1 plan still needs topology prechecks, distance fields, and
field reuse to amortize many-agent path work.

## Current Profiling Notes

Large open-grid benchmarks currently expand one node per path coordinate, but
reach roughly 2.5x more nodes because neighbor candidates are discovered around
the corridor. On a 1024x1024 open 2D world from corner to corner, the Release
benchmark reports a 2,046 unit path, 2,047 expanded nodes, and 5,112 reached
nodes.

Sampling the 1024x1024 query shows time concentrated in neighbor processing:
heap maintenance for the open set, passability/world lookup for each accepted
neighbor candidate, and fixed six-axis neighbor generation even for degenerate
2D shapes. Those are the first optimization targets before treating this A*
path as suitable for hundreds of independent agents per tick.
