# TDD: Topology and Region Graph

## 1. Summary

This document defines the topology and region graph system.

Topology answers:

- Is this tile passable?
- Which connected region contains this tile?
- Can movement type M reach tile B from tile A?
- Which portals connect local regions across chunks?
- Which topology products are invalidated by terrain/building changes?

The topology system is designed for a 3D internal world model with degenerate dimensions handled gracefully.

## 2. Goals

- Support fast reachability prechecks before pathfinding.
- Support local chunk topology and inter-chunk portal graph.
- Support explicit movement transitions rather than naive 3D adjacency.
- Support top-down 2D, vertical 2D, and true 3D worlds.
- Support sparse/resident chunks and missing chunk policies.
- Support incremental dirty rebuilds.
- Version topology products for path cache invalidation.
- Avoid throwaway 2D-only topology code.

## 3. Non-goals

- No full pathfinding implementation here.
- No room ownership/room stats here.
- No fluid/gas simulation here.
- No entity-per-tile topology.
- No dynamic world dimensions.
- No automatic full-volume topology rebuild for huge worlds.
- No assumption that adjacent TileKeys are adjacent tiles.
- No assumption that 3D movement uses all 26 neighboring cells.

## 4. Core concepts

- Passability
- MovementMask / MovementClass
- Transition
- TransitionProvider
- LocalRegion
- Portal
- RegionGraph
- TopologyVersion
- DirtyTopology

## 5. Topology stages

Stage A: local chunk topology.

Stage B: inter-chunk portal graph.

Stage C: vertical/special transition providers.

Stage D: hierarchical/coarse topology.

Each stage is useful and progresses toward final design.

## 6. Movement model

Movement is graph-based.

A transition is legal if source/target are valid, movement mask permits transition type, passability permits movement, unit size/flags permit occupancy, and topology flags permit it.

Transition types include cardinal, diagonal, stair, ramp, ladder, drop, fly, swim, door, bridge, and custom.

## 7. Game-defined topology vocabulary

Internally, topology operates on opaque compile-time bit patterns/masks. The game defines names and rules through a compile-time domain language.

Example:

```cpp
using namespace tiles::topo::dsl;

constexpr auto Solid = tag<"solid">();
constexpr auto Wall = tag<"wall">();
constexpr auto Water = tag<"water">();
constexpr auto StairUp = tag<"stair_up">();

constexpr auto Walk = movement<"walk">();
constexpr auto Fly = movement<"fly">();
constexpr auto Swim = movement<"swim">();

constexpr auto rules = topology_rules(
  movement_class<"pawn">()
    .can(Walk)
    .blocked_by(Solid | Wall | Water)
    .uses(StairUp),

  movement_class<"bird">()
    .can(Fly)
    .blocked_by(Solid | Wall),

  movement_class<"fish">()
    .can(Swim)
    .requires(Water)
);
```

The substrate compiles this to masks, transition tables, debug names, and schema/version hashes.

## 8. Degenerate dimensions

Disabled axes produce no normal neighbor transitions. Topology must not assume x/y is always the main plane.

Supported cases:

- top-down 2D: x/y movement
- vertical 2D x/z
- vertical 2D y/z
- vertical column

## 9. Local chunk topology

Inputs:

- passability field
- movement cost field, optional
- terrain/building flags
- transition provider
- chunk bounds
- neighboring chunk metadata, optional

Outputs:

- local_region_id per local tile
- local region summaries
- boundary exits
- special transition exits
- dirty topology version

## 10. Portal graph

Portals connect local regions.

Portal types:

- adjacent chunk face
- stair/ramp/ladder/drop
- flying edge
- door
- bridge
- custom

The graph is movement-aware.

## 11. Region graph

Operations:

- same_region
- reachable
- region_of
- portals
- neighboring_regions
- coarse_path

Reachability result distinguishes:

- Reachable
- Unreachable
- UnknownMissingChunks
- RequiresGeneration
- InvalidStartOrGoal

## 12. Dirty topology and versioning

Dirty topology is triggered by terrain passability, building placement/removal, door/bridge state, ramp/stair modification, chunk generation/load/evict, movement rule changes, and topology config changes.

Maintain global, movement-layer, chunk, portal graph, and local region versions.

## 13. Sparse/resident chunk behavior

Topology queries respect missing chunk policy:

- FailIfMissing
- MetadataOnly
- GenerateIfMissing
- ApproximateIfMissing

## 14. Interaction with pathfinding

Pathfinding uses topology for:

- early unreachable rejection
- coarse route over region graph
- corridor selection
- limiting distance field bounds
- cache invalidation

A* still uses TransitionProvider for exact movement.

## 15. API sketch

```cpp
class Topology {
public:
  TopologyVersion version(MovementClass movement) const;
  Reachability reachable(TileKey a, TileKey b, MovementClass movement) const;
  RegionRef region_of(TileKey tile, MovementClass movement) const;
  Span<Portal> portals(RegionRef region) const;
  void mark_dirty(Box3 bounds, DirtyReason reason);
  TopologyRebuildResult rebuild_dirty(World& world, Domain domain);
};
```

## 16. Diagnostics

Report dirty chunks, local regions, portals, version changes, rebuild time, graph size, failed reachability prechecks, missing chunks, vertical transition counts, and degenerate-axis pruning.

## 17. Performance concerns

- local chunk rebuild preferred over full graph rebuild
- portal graph size can grow in 3D
- many movement classes can multiply products
- sparse missing chunks can cause stalls
- degenerate worlds should not pay for disabled axes in hot loops

## 18. Tests

Test local regions, portals, dirty marking, reachability, stale regions, missing chunks, doors/walls, stairs/ramps, top-down 2D, vertical 2D, and degenerate-axis correctness.

## 19. Benchmarks

- local chunk rebuild
- vertical 2D rebuild
- 3D rebuild
- portal graph build/repair
- reachability query
- stair/ramp edits
- many movement classes

## 20. Acceptance criteria

- Local chunk topology works for 2D, vertical 2D, and 3D.
- Portal graph connects local regions across chunks.
- Explicit transition providers support vertical/special movement.
- Reachability precheck rejects impossible paths cheaply.
- Dirty rebuilds avoid full-world scans.
- Topology versions invalidate dependent products.
- Game-defined topology vocabulary compiles to opaque masks/tables.
