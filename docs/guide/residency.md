---
description: >-
  Choosing tess world storage: always-resident versus sparse residency
  under a fixed memory budget, and what each supports.
---

# Residency

**The decision:** can every chunk of the world stay allocated within your
memory budget? When unsure, pick `AlwaysResidentWorld` — switching later
is a type alias change plus handling for the sparse-only states below.

## Branches

| Branch | Pick when | You commit to |
| --- | --- | --- |
| `AlwaysResidentWorld` | full storage fits the budget and most chunks are actually used | nothing extra: residency is a no-op, every query is answerable |
| `SparseResidentWorld` | full storage exceeds the budget, or occupancy is a small fraction of the shape | a `ResidencyConfig` byte budget, explicit residency management, LRU eviction, handling `PathStatus::Indeterminate` via a `MissingChunkPolicy`, and losing the dense-only distance-field products ([pathfinding](pathfinding.md)) |

## Thresholds

The Tess-owned inline page-storage cost is a compile-time constant — print or
`static_assert` `World::storage_byte_size` before deciding. It excludes any
dynamic storage or referenced objects managed by field values. For the sparse
branch, capacity is `byte_budget / page_byte_size` chunks: size
the budget from the number of *simultaneously hot* chunks, not the world
size, or the LRU will thrash.

## What it looks like

<!-- tess-snippet: storage-dense-world source=examples/documentation.cc -->
```cpp
tess::AlwaysResidentWorld<MyShape, MySchema> dense_world;
auto pages = dense_world.chunks();
auto& page = dense_world.chunk(tess::ChunkKey{3});
auto resolved = dense_world.resolve(tess::Coord3{10, 20, 0});
dense_world.field<TerrainTag>(tess::Coord3{10, 20, 0}) = 7;
auto terrain = dense_world.field_span<TerrainTag>(tess::ChunkKey{3});
```
<!-- /tess-snippet -->

<!-- tess-snippet: sparse-budget source=examples/sparse_stream.cc -->
```cpp
constexpr auto budget = 16 * HugeSparse::page_byte_size;
HugeSparse world{tess::ResidencyConfig{budget}};  // Materializes no chunks.

for (std::uint64_t key = 0; key < 64; ++key) {
  (void)world.ensure_resident(tess::ChunkKey{key});  // LRU-evicts at cap.
}

static_assert(HugeDense::storage_byte_size == 64 * budget);
const auto ok = world.resident_byte_size() <= world.byte_budget();
```
<!-- /tess-snippet -->

<!-- tess-snippet: sparse-indeterminate source=examples/sparse_stream.cc -->
```cpp
tess::PathScratch scratch;
const auto pending = tess::astar_path<StreamWorld, PassableTag>(
    world, request, scratch, tess::MissingChunkPolicy::ReportIndeterminate);
// pending.status == PathStatus::Indeterminate: a non-resident chunk was
// skipped, so failure is not proven -- materialize the chunk and retry.

(void)world.ensure_resident(tess::ChunkKey{1});
open_row(world, 32, 63);
const auto found = tess::astar_path<StreamWorld, PassableTag>(
    world, request, scratch, tess::MissingChunkPolicy::ReportIndeterminate);
```
<!-- /tess-snippet -->

## Learn and specify

- Teach: [getting-started §3](../getting-started.md), rung 3.
- Specify: [storage note](../architecture/storage.md) (residency and
  eviction), [pathfinding note](../architecture/path.md) (sparse
  coverage semantics).

## Scope

Sparse worlds materialize chunks on demand under the byte budget. Tess archives
the current resident set; applications own durable backing storage and decide
when and from where non-resident chunk data is supplied.
