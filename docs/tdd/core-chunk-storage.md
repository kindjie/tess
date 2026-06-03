# TDD: Core Chunk Storage

## 1. Summary

This document defines the chunk storage layer for the tile/path simulation substrate.

The storage layer owns resident chunk pages, chunk-local tile fields, dirty/active masks, chunk metadata, and hot-loop access patterns.

The system is internally 3D. Degenerate dimensions are valid:

- `z == 1`: traditional 2D top-down map
- `x == 1` or `y == 1` with `z > 1`: vertical 2D / side-view world
- any dimension with extent 1 should collapse cleanly in traversal, topology, and field products

## 2. Goals

- Store tile fields in chunk-local SoA arrays.
- Support constexpr world/chunk shape.
- Support dense always-resident and sparse-resident worlds through derived traits.
- Make hot block/chunk iteration allocation-free.
- Avoid global coordinate/key decode in inner loops.
- Support huge bounded worlds without full allocation.
- Support sleeping/resident/active chunk states.
- Provide dirty/active chunk sets for the queued planner.

## 3. Non-goals

- No runtime world dimensions.
- No unbounded coordinate space.
- No entity-per-tile storage.
- No per-tile virtual dispatch.
- No automatic full-world materialization for sparse worlds.
- No renderer-owned presentation state.
- No GPU storage implementation in v1.
- No dynamic field schema changes after world type creation.

## 4. Core concepts

World, ChunkPage, ChunkHandle, LocalTileId, Field, ChunkDirectory, and Block.

## 5. Storage modes

### AlwaysResident

All chunks are allocated at world creation.

Properties:

- no missing chunks
- no generation or eviction
- chunk lookup can be direct dense indexing
- dense field products allowed if they fit budget

### SparseResident

Logical world is bounded, but not all chunks are resident.

Properties:

- chunk directory stores generated/resident state
- missing chunks are possible
- generation/loading can be queued
- dense full-world products are rejected by default
- products should be chunked/sparse

## 6. Chunk lifecycle

States:

- Unallocated
- GeneratedMetadata
- ResidentSleeping
- ResidentActive
- SerializedEvictable
- Evicted

Sleeping means simulation sleep, not necessarily disk eviction.

## 7. Chunk shape

Chunk dimensions are compile-time powers of two. World dimensions must be multiples of chunk dimensions.

Recommended starting defaults:

- small top-down 2D: one chunk if <= 1024x1024x1
- larger top-down 2D: 64x64x1
- true 3D: 32x32x4
- vertical 2D: benchmark shapes such as 64x1x16, 64x1x64, 1x64x16, 1x64x64

Rule: chunk shape should follow the active simulation plane. Degenerate dimensions should not force cubic chunks.

## 8. Chunk-local layout

Inside each chunk, fields are row-major SoA.

```text
local_id = local_x + local_y * chunk.x + local_z * chunk.x * chunk.y
```

Hot kernels access fields through chunk-local spans.

## 9. Field storage

Fields are declared in the compile-time field schema.

Field categories:

- core: terrain, passability, movement_cost, occupancy, region_id
- path/topology: local_region_id, portal_id, reservation_penalty
- simulation: temperature, gas, liquid, fire, light
- debug/render: danger, visibility, render_tile_index

Fields may be authoritative or derived products.

## 10. Field handles

Field access uses typed handles. String/name lookup is allowed only for setup, debugging, serialization, and editor/tool display.

## 11. Chunk directory

AlwaysResident worlds can use direct dense indexing.

SparseResident worlds use chunk state table, resident chunk map, optional hierarchy/SFC index, generated metadata store, and eviction/serialization hooks.

Hot loops receive ChunkHandle from the planner and should not perform directory lookup per tile.

## 12. Masks and dirty state

Masks exist at global/chunk and per-chunk levels.

Common masks:

- dirty_terrain
- dirty_walkability
- dirty_topology
- dirty_cost
- dirty_render
- active_fluid
- active_fire
- active_temperature
- active_entities

Sparse worlds should use chunked masks rather than full-world bitsets unless explicitly compact.

## 13. Chunk metadata

Required:

```cpp
struct ChunkMeta {
  ChunkState state;
  uint32_t version;
  uint32_t topology_version;
  uint32_t field_dirty_flags;
  uint32_t active_flags;
  Box3 dirty_bounds;
  uint32_t active_count;
  uint32_t entity_count;
};
```

Optional: summaries, portal summary, last active tick, generation seed/version, eviction score.

## 14. Block generation

Planner groups chunks into blocks from domains such as:

- resident chunks
- dirty chunks
- active chunks
- visible chunks
- Box3
- path corridor
- explicit chunk list

Default ordering:

- single chunk: trivial
- 2D chunked: Morton over effective axes
- 3D chunked: Morton3D
- Hilbert: benchmark candidate

## 15. Degenerate dimensions

Any dimension of size 1 should simplify cleanly.

Rules:

- local index math remains valid
- bounds remain Box3
- neighbor providers ignore degenerate axes unless explicitly meaningful
- path topology should not assume x/y is always the main plane
- render deltas preserve actual coordinates

## 16. Residency and generation hooks

Storage exposes hooks for future sparse/generation support:

- ensure_resident
- generate_metadata
- load_chunks
- serialize_chunks
- evict_chunks
- sleep_chunks
- wake_chunks

Missing chunk policies:

- FailIfMissing
- RequireResident
- GenerateMetadataOnly
- GenerateFull
- ApproximateIfMissing

## 17. Threading model

Rules:

- multiple readers allowed
- writes require declared ownership/policy
- no structural chunk allocation/eviction during active block iteration
- thread-local dirty masks/events merge after barriers
- block jobs operate on resolved ChunkHandles

## 18. Serialization

Serialization records:

- shape metadata
- field schema version
- chunk state
- generated/resident chunk contents
- dirty/persisted flags
- optional metadata-only chunks
- key layout version

Sparse worlds save only generated/modified chunks, not empty logical space.

## 19. API sketch

World exposes:

- contains
- key/coord
- try_resolve/resolve_unchecked
- field<T>
- chunk_state
- try_chunk
- resident_chunks
- dirty_chunks
- active_chunks

ChunkPage exposes:

- key
- coord
- state
- field_span<T>
- local_coord/local_id
- metadata

## 20. Diagnostics

Report:

- total logical chunks
- resident/active/sleeping chunks
- estimated full-resident bytes
- actual resident bytes
- bytes per chunk/field
- dirty counts
- chunk lifecycle transitions
- missing chunk requests
- full-world warnings
- unexpected allocations

## 21. Performance concerns

- Chunk size affects cache locality, path boundary overhead, dirty granularity, topology rebuild cost, scheduling overhead.
- Adding fields may change inferred residency.
- Planner resolves chunks before execution.
- Dirty/active masks must avoid full-world bitsets for huge worlds.
- Expose spans and block domains so higher layers avoid unnecessary writes.

## 22. Tests

Compile-time and runtime tests cover top-down 2D, vertical 2D, true 3D, huge sparse bounded worlds, invalid shape constraints, dirty masks, block generation, chunk lifecycle, and serialization metadata.

## 23. Benchmarks

- flat dense baseline vs single-chunk World
- chunked field iteration
- dirty chunk iteration
- field span acquisition
- chunk directory lookup
- sparse mask operations
- vertical 2D chunk layout variants
- always-resident vs sparse-resident access

## 24. Acceptance criteria

- Static shape constraints are enforced at compile time.
- 2D, vertical 2D, and 3D shapes use the same storage model.
- Single-chunk 2D has near-flat-array overhead.
- Hot field iteration uses chunk-local contiguous spans.
- Sparse huge worlds do not allocate full logical volume.
- Dirty/active chunk domains can be produced without scanning all tiles.
- Diagnostics expose memory estimates and storage decisions.
