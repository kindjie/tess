# Project Design Doc: Queued Tile/Path Simulation Substrate

## 1. Summary

This project is a performance-first C++ substrate for tile-based simulation, pathfinding, topology, and spatial world data in colony sims, city builders, 4X games, roguelikes, automation games, Dwarf-Fortress-like worlds, and related simulation-heavy genres.

The primary interface is queued intent:

- game systems enqueue operations and queries
- the library validates, batches, and plans execution
- the executor runs block/chunk-local work efficiently
- results are committed at deterministic phase boundaries

The library owns dense/sparse tile substrate mechanics, topology/path products, scheduling/planning infrastructure, diagnostics, and integration hooks. Game code owns content, ECS state, pawn AI meaning, rendering, presentation policy, and gameplay rules.

## 2. Goals

- Provide fast chunked tile storage for 2D and 3D worlds.
- Use a 3D internal coordinate model; degenerate dimensions are valid.
- Support huge bounded worlds with sparse generated/resident chunks.
- Make performant usage easy and slow usage explicit.
- Prioritize pathfinding, reachability, and flow/distance fields.
- Use queued operations/queries as the normal game-facing API.
- Support fixed-TPS simulation with different subsystem cadences and speed multipliers.
- Integrate cleanly with EnTT in v1; keep core ECS-agnostic.
- Publish render/presentation deltas without mandatory full snapshots.
- Prepare compile-time GPU backend interfaces without requiring GPU execution in v1.
- Use modern C++ and Clang-friendly diagnostics for compile-time safety.

## 3. Non-goals

- Do not replace an ECS.
- Do not make every tile an entity.
- Do not own the renderer, animation system, or presentation state.
- Do not require runtime world dimensions.
- Do not support unbounded/infinite coordinate spaces in v1.
- Do not require C++26 reflection or contracts.
- Do not hide full-world scans behind ergonomic APIs.
- Do not clone the whole world every tick for rendering.
- Do not make GPU authoritative for gameplay.
- Do not solve pawn AI or game content; provide the substrate for them.
- Do not implement Flecs adapter, tactical assignment, full crowd steering, or real GPU compute in v1.

## 4. Core decisions

### 4.1 World shape is constexpr-only

World dimensions and chunk dimensions are compile-time constants. The game developer specifies world size and chunk size; the substrate derives implementation traits.

Conceptual syntax:

```cpp
using World = tiles::World<
  tiles::Shape{
    .size = {1024, 1024, 1},
    .chunk = {64, 64, 1}
  },
  Fields
>;
```

If aggregate NTTP syntax is awkward, use a named shape type with static constexpr members.

### 4.2 3D-only internal model

All worlds are internally 3D. Degenerate dimensions are valid:

- `z == 1`: traditional top-down 2D
- `x == 1` or `y == 1` with `z > 1`: vertical 2D / side-view world
- multiple axes of size 1: line/column worlds

Disabled axes should collapse cleanly in storage, topology, pathing, field products, and render deltas.

### 4.3 Chunk constraints

Chunk dimensions must be powers of two. World dimensions must be exact multiples of chunk dimensions.

Reasons:

- faster chunk/local coordinate math
- simpler packed TileKey layout
- simpler topology/product chunking
- no edge chunk special cases
- clearer compile-time diagnostics

### 4.4 Derived traits

The user specifies:

- world size
- chunk size
- field schema
- optional resident memory budget

The substrate derives:

- effective dimensions
- chunk counts
- local tile count
- TileKey width: u64 or u128
- local id bit width
- chunk key bit width
- residency mode
- dense vs sparse product defaults
- whether missing chunks are possible
- whether vertical topology is enabled

### 4.5 Residency is inferred

Residency should normally be inferred from world shape, field schema, and memory budget.

If estimated full-resident bytes fit the budget:

- AlwaysResident

Otherwise:

- SparseResident

Advanced overrides may exist:

- Auto
- RequireAlwaysResident
- RequireSparseResident

### 4.6 TileKey is canonical identity

The system uses a canonical one-dimensional TileKey internally.

Game-facing APIs may accept Coord2 or Coord3 convenience values.

Hot execution uses:

- ChunkHandle
- LocalTileId
- typed field spans

Changing shape/key layout is a save-compatibility event requiring explicit migration or invalidation.

## 5. High-level architecture

- `tiles::core`: shape traits, coordinates, keys, fields, masks, chunks, spatial index
- `tiles::queued`: FrameOps, operation schemas, planner, execution graph, result channels
- `tiles::sim`: fixed tick scheduler, cadence rules, sim speed
- `tiles::topo`: local regions, portal graph, transition providers, topology versions
- `tiles::path`: A*, path tickets, scratch, path cache hooks
- `tiles::fields`: distance, flow, influence, congestion products
- `tiles::block`: expert block-local kernels
- `tiles::pipe`: internal/advanced lazy block pipelines
- `tiles::ecs::entt`: optional v1 EnTT adapter
- `tiles::delta`: render delta frames and baselines
- `tiles::gpu`: compile-time backend concepts and descriptors only in v1
- `tiles::diag`: traces, warnings, stats, plan dumps
- `tiles::debug::imgui`: optional Dear ImGui panels
- `tiles::unsafe`: explicit escape hatches only

## 6. Storage model

World data is stored in chunks. Chunk-local fields use SoA layout. Hot loops operate on chunk-local spans rather than global coordinate lookups.

Small 2D example:

```cpp
size = {1024, 1024, 1}
chunk = {1024, 1024, 1}
```

This naturally becomes one always-resident chunk.

Huge sparse example:

```cpp
size = {1'000'000, 1'000'000, 256}
chunk = {32, 32, 4}
```

This becomes a bounded logical world where only generated/resident chunks consume memory.

## 7. Queued operation model

Game systems enqueue intent:

- query paths
- query nearest reachable target
- move entities
- place building
- update field
- rebuild topology
- request distance field
- publish render deltas

The library decides how to batch and execute.

Operations declare:

- reads
- writes
- domain
- write policy
- versions
- priority
- budget behavior
- backend eligibility
- exactness requirement
- source location

## 8. Simulation scheduler

Simulation runs at fixed TPS. Rendering is separate.

Scheduler supports:

- every tick
- every N ticks
- when dirty
- event-triggered
- budgeted background
- variable speed by processing more fixed ticks per real second

The fixed simulation timestep does not change at 2x/5x speed.

## 9. Topology and pathfinding

Topology is staged:

1. local chunk topology
2. inter-chunk portal graph
3. vertical/special transition providers
4. hierarchical/coarse topology

Movement is defined by game-provided compile-time topology vocabulary compiled to opaque internal bit patterns and transition tables.

Pathfinding uses:

- topology prechecks
- A*
- reusable scratch
- path tickets
- versioned PathView for UI/debug overlays
- distance/flow fields when reuse wins

## 10. Flow, distance, congestion, tactical fields

Flow/distance products are derived fields used when many agents share goals, corridors, bottlenecks, or tactical constraints.

Supported concepts:

- distance fields
- flow fields
- influence/danger/visibility fields
- congestion/bottleneck fields
- future tactical query and assignment products

Full-volume fields are rejected by default for huge worlds.

## 11. ECS integration

Tiles are not ECS entities.

v1 targets:

- ECS-agnostic core
- EnTT adapter
- custom minimal adapter example

Flecs is deferred.

ECS systems inspect components, enqueue intents, and consume result channels after execution.

## 12. Render delta model

The library does not clone the world for rendering.

Default output:

- DeltaFrame
- tick/version header
- entity motion deltas
- spawns/despawns
- tile dirty regions
- field change descriptors
- visual events
- optional baseline/resync

The renderer owns presentation state.

## 13. GPU strategy

GPU interfaces are designed early, but implementation is deferred. CPU remains authoritative.

The game/plugin supplies a compile-time backend type satisfying the substrate’s GPU backend concepts. The backend wraps the game’s graphics/compute abstraction.

## 14. Diagnostics and tooling

Diagnostics are first-class and structured.

Optional Dear ImGui module should provide substrate-specific panels:

- world overview
- chunk/tile inspector
- planner trace
- path debug
- topology debug
- field/product cache
- scheduler
- render delta
- GPU status

Existing EnTT/ImGui ecosystem tools can handle generic entity/component inspection.

## 15. Save/load compatibility

Saves should record:

- shape metadata or shape ID
- chunk dimensions
- key layout version
- field schema version
- topology/product schema versions
- library version

TileKeys may be persisted. Incompatible shape/key/schema changes require explicit migration or invalidation. Important long-lived references may store Coord3 for debug/migration.

## 16. Milestones

v1 includes:

- shape/key system
- chunk storage
- typed fields
- block executor
- queued ops/planner foundation
- scheduler with sim speed support
- local topology + portal foundation
- A*
- distance field + nearest query
- byte-budgeted product cache
- EnTT adapter
- render delta bridge
- diagnostics
- benchmark suite
- GPU backend interfaces

Deferred:

- Flecs adapter
- real GPU backend
- tactical query/assignment module
- crowd/local steering module
- room/area system
- save/load migration TDD
- editor/tool integration beyond optional ImGui panels

## 17. Core principle

The public API exposes intent.

The library owns execution.

The game owns meaning.
