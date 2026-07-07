# Storage Foundation

The implemented storage foundation covers typed compile-time field schemas,
resident chunk pages, an always-resident dense world owner, and per-chunk
metadata for dirty/active tracking. It implements the early storage slices of
the historical [chunk storage TDD](../tdd/core-chunk-storage.md).

## Field Schemas

Tile fields are declared with `tess::Field<Tag, Value>`, where `Tag` is a
user-defined type and `Value` is the stored tile value type. Tags are type
handles; string or name lookup remains outside the current implementation.

`tess::FieldSchema<Fields...>` rejects duplicate tag types at compile time and
exposes:

- `field_count`
- `contains<Tag>`
- `value_type<Tag>`

The public `tess::is_valid_field_schema_v<Fields...>` helper lets tests and
future metaprogramming code detect duplicate tags without intentionally
instantiating an invalid schema.

## Chunk Page

`tess::ChunkPage<Shape, Schema>` stores one resident chunk worth of tile data.
Fields are owned directly by the page object as chunk-local SoA arrays:

```cpp
std::array<Value, tess::ShapeTraits<Shape>::local_tile_count>
```

The page performs no runtime allocation. Hot code can access each field through
contiguous typed spans:

```cpp
auto terrain = page.field_span<TerrainTag>();
page.field<CostTag>(tess::LocalTileId{42}) = 3.0F;
```

Const pages return `std::span<const Value>` and `const Value&`.

## Metadata

Each page stores the chunk identity passed at construction:

- `ChunkKey`
- `ChunkCoord3`

The page type also exposes:

- `local_tile_count`
- `field_count`
- `byte_size`

`byte_size` reports the owned field-array storage, not object padding or
metadata bytes.

## Always-Resident World

`tess::World<Shape, Schema, tess::AlwaysResident>` owns one
`tess::ChunkPage<Shape, Schema>` for every chunk in the shape. The convenience
alias `tess::AlwaysResidentWorld<Shape, Schema>` names the same type.

Pages live in a `std::vector` populated during world construction. Construction
may allocate and throw. The vector is filled in `ChunkKey` order, and each page
stores matching `ChunkKey` and `ChunkCoord3` metadata derived from the public
shape key conversion helpers.

The world type exposes static storage metadata:

- `chunk_count`
- `local_tile_count`
- `field_count`
- `page_byte_size`
- `storage_byte_size`

Hot accessors are explicitly `noexcept` and do not allocate after
construction:

```cpp
tess::AlwaysResidentWorld<MyShape, MySchema> world;
auto pages = world.chunks();
auto& page = world.chunk(tess::ChunkKey{3});
auto resolved = world.resolve(tess::Coord3{10, 20, 0});
world.field<TerrainTag>(tess::Coord3{10, 20, 0}) = 7;
auto terrain = world.field_span<TerrainTag>(tess::ChunkKey{3});
```

Unchecked accessors require valid chunk keys, chunk coordinates, and tile
coordinates. Checked `try_*` accessors return `nullptr` or `std::nullopt` for
out-of-bounds input.

## Chunk Metadata

Each always-resident world owns one `tess::ChunkMeta` per resident page in
matching `ChunkKey` order. Metadata defaults to sleeping, clean, inactive
chunks:

- `state = tess::ChunkState::ResidentSleeping`
- `version = 0`
- `topology_version = 0`
- `field_dirty_flags = 0`
- `active_flags = 0`
- `dirty_bounds = {}`
- `active_count = 0`
- `entity_count = 0`

Direct metadata lookup mirrors page lookup:

```cpp
auto& meta = world.meta(tess::ChunkKey{3});
auto* checked = world.try_meta(tess::ChunkCoord3{3, 0, 0});
```

These direct accessors are `noexcept` and do not allocate. Dirty and active
flags are raw `std::uint32_t` masks in this slice. `mark_dirty` unions dirty
bounds and increments the chunk version; `clear_dirty` clears selected bits and
resets bounds when no dirty bits remain. `mark_active` and `clear_active`
maintain `active_count` and move the chunk between sleeping and active state
when the active flag set becomes nonzero or empty.
`mark_topology_dirty` applies dirty metadata and increments both the chunk
version and topology version. `mark_topology_rebuilt` increments only the
topology version so topology products can observe rebuild/replacement events.

Maintenance passes that rebuild derived state use the generation-stamped
observe/clear pair instead of raw `clear_dirty`. `observe_dirty(key, flags)`
snapshots the requested dirty subset, the dirty bounds, and the chunk version
into a `DirtyObservation`. `clear_dirty_observed(key, observation)` clears
exactly the observed flags only while the chunk version still matches the
observation; any `mark_dirty` that lands after the observation advances the
generation, so a stale clear leaves every flag and bound in place and returns
`false`, and the caller re-observes before clearing. This is the dirty
metadata protocol required before concurrent or budgeted maintenance may
clear flags it did not fully rebuild.

`dirty_chunks(flags)` and `active_chunks(flags)` return matching `ChunkKey`
values in key order. These query helpers allocate their returned vectors; they
are intended for planner/domain construction, not inner tile loops.
`collect_dirty_chunks(flags, out)` and `collect_active_chunks(flags, out)`
append the same keys to a caller-owned vector and do not allocate when the
caller has reserved enough capacity; the by-value queries are thin wrappers
over them.

## Out Of Scope

This slice does not implement sparse residency, `ChunkDirectory`, generation or
eviction, typed dirty/active vocabularies, full lifecycle states beyond
sleeping/active, thread ownership policies, block generation, or planner
domains.
