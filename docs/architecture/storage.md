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

## Sparse-Resident World

`tess::World<Shape, Schema, tess::SparseResident>` (alias
`tess::SparseResidentWorld<Shape, Schema>`) materializes only a byte-budgeted
subset of a bounded shape, so a world spanning trillions of chunks costs only
its residency budget rather than `chunk_count` pages. It is constructed with a
`tess::ResidencyConfig{byte_budget}`; the resident capacity is
`byte_budget / page_byte_size` (at least one chunk). Construction allocates the
fixed slot pool and directory once and never reallocates them.

Residency is managed explicitly:

```cpp
tess::SparseResidentWorld<HugeShape, MySchema> world{
    tess::ResidencyConfig{16 * world.page_byte_size}};
const auto handle = world.ensure_resident(tess::ChunkKey{k});
world.chunk(tess::ChunkKey{k}).field<TerrainTag>(tess::LocalTileId{0}) = 7;
```

- `ensure_resident(key)` materializes the chunk (evicting the
  least-recently-used chunk when the budget is full), marks it
  most-recently-used, and returns a `tess::ResidencyHandle`. It is idempotent:
  an already resident chunk keeps its data and generation. An intrusive
  doubly-linked LRU over slot indices makes victim selection and recency
  updates O(1), independent of the resident capacity; its link arrays are
  allocated with the fixed slot pool. A budget smaller than one page clamps
  the capacity to one chunk rather than producing an unusable zero-capacity
  world.
- `touch(key)` refreshes recency; `evict(key)` releases a chunk immediately.
- `is_resident(key)` distinguishes a resident chunk from a `Missing` one;
  `contains(key)` reports only in-bounds-ness. Both differ from out-of-bounds.
- Unchecked `chunk`/`meta` accessors require residency; `try_chunk`/`try_meta`
  and `try_field` return `nullptr` for a non-resident (or out-of-bounds) chunk
  — the residency-tolerant readers later slices build missing-chunk policy on.

Eviction and reload are generation-safe. Each residency assigns a
world-monotonic generation that is never reused, so a `ResidencyHandle` taken
before an eviction never validates (`world.valid(handle)` returns `false`)
against the reloaded chunk, which reuses the key but receives a strictly
greater generation. A reloaded chunk is a fresh, zeroed page — evicted data is
gone. `residency_generation(key)` returns 0 for a non-resident chunk.

`resident_chunk_keys()` enumerates exactly the resident set, and the
dirty/active queries and `mark_*` helpers behave identically to the dense world
but iterate only resident chunks. No accessor or query scans `0..chunk_count`,
preserving the "no hidden full-world scans" invariant at sparse scale. The
directory is a fixed-capacity open-addressing map with backward-shift deletion,
so long-lived evict/reload churn allocates nothing and accumulates no
tombstones. Both worlds share one `ChunkMeta` mutation implementation
(`tess/storage/chunk_meta.h`), so dirty/active/version semantics are identical.

## Chunk Metadata

Each always-resident world owns one cold `tess::ChunkMeta` per resident page in
matching `ChunkKey` order, plus world-owned parallel arrays for fields scanned
frequently. A new chunk therefore has this combined state:

- `state = tess::ChunkState::ResidentSleeping`
- `version = 0`
- `topology_version = 0`
- `active_count = 0`
- `entity_count = 0`
- `world.dirty_flags(key) = 0`
- `world.active_flags(key) = 0`
- `world.dirty_bounds(key) = {}`

Direct metadata lookup mirrors page lookup:

```cpp
auto& meta = world.meta(tess::ChunkKey{3});
auto* checked = world.try_meta(tess::ChunkCoord3{3, 0, 0});
```

These direct accessors are `noexcept` and do not allocate, but a `ChunkMeta`
reference does not contain the complete dirty/active state. Dirty flags, active
flags, and dirty bounds must be read through the world accessors and mutated
through its `mark_*`, `clear_*`, and observation APIs. Keeping those hot-scan
columns out of `ChunkMeta` avoids pulling cold counters into bulk queries.

Dirty and active flags are raw `std::uint32_t` masks. `mark_dirty` unions dirty
bounds and increments the chunk version; `clear_dirty` clears selected bits and
resets bounds when no dirty bits remain. `mark_active` and `clear_active`
maintain `ChunkMeta::active_count` and move the chunk between sleeping and
active state when the active flag set becomes nonzero or empty.
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

Sparse residency, the `ChunkDirectory`, per-chunk generations, and byte-budget
eviction are implemented (see Sparse-Resident World). The
[topology](topology.md) and [path](path.md) layers build on the
residency-tolerant `try_*` readers to run supported APIs natively over sparse
worlds and report `Indeterminate` when unloaded space prevents a definitive
answer. Still out of scope in this layer: typed dirty/active vocabularies, full
lifecycle states beyond sleeping/active, on-demand chunk materialization
policy, thread ownership policies, block generation, and planner domains.
