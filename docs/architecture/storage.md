# Storage Foundation

The implemented storage foundation covers one resident chunk page and typed
compile-time field schemas. It is the first implementation slice of the
historical [chunk storage TDD](../tdd/core-chunk-storage.md).

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

## Out Of Scope

This slice does not implement `World`, `ChunkDirectory`, sparse residency,
generation or eviction, dirty/active masks, lifecycle states, thread ownership
policies, block generation, or planner domains.
