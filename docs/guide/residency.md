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

The full-storage cost is a compile-time constant — print or
`static_assert` `World::storage_byte_size` before deciding. For the
sparse branch, capacity is `byte_budget / page_byte_size` chunks: size
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

## Learn and specify

- Teach: [getting-started §3](../getting-started.md), rung 3.
- Specify: [storage note](../architecture/storage.md) (residency and
  eviction), [pathfinding note](../architecture/path.md) (sparse
  coverage semantics).

## Horizon

!!! note "Planned"
    The full sparse generation/streaming/serialization lifecycle is
    design-forward (see the [roadmap](../roadmap.md)); shipped sparse
    worlds materialize on demand under the byte budget.
