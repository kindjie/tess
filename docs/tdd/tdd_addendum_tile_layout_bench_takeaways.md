# TDD Addendum: Tile Layout Benchmark Findings and Library Integration

## Status

Proposed addendum. This document does not duplicate the benchmark repository. It defines which findings should influence the tile-map library, how to validate them independently, and when to promote benchmark-inspired techniques into production APIs.

## Reference

Primary reference repository:

```text
https://github.com/kindjie/tile-layout-bench
```

Use repository-relative documentation links where possible so references remain resilient as the repo evolves:

```text
README.md
RESULTS.md
docs/layouts.md
docs/relationships.md
docs/roadmap.md
```

Treat the benchmark repo as living evidence, not frozen specification. If results change, this library addendum should be updated by changing decisions and success criteria, not by copying benchmark internals into this document.

## Context

The tile-map library needs fast operations for colony-sim, city-builder, 4X, and management-game workloads. Earlier design work considered whether exotic 2D-to-1D layouts such as Hilbert, Morton, Sierpinski, or other space-filling curves should be central to the storage model.

The benchmark repository suggests a more practical direction:

```text
Prefer simple canonical storage and workload-specific acceleration structures.
Use exotic layouts as optional experiments, not default architecture.
```

The library should absorb the benchmark's conclusions as design pressure, while preserving its own API stability and correctness guarantees.

## Existing Design Alignment

The current library is already 3D-first and chunk-first:

```text
Shape<Extent3, Extent3>
Coord3 / Box3
ChunkCoord3 / ChunkKey
LocalCoord3 / LocalTileId
FieldSchema<Field<Tag, Value>...>
AlwaysResidentWorld<Shape, Schema>
```

This addendum must not be read as a request to replace those concepts with a
separate 2D-only layout vocabulary. Benchmark examples use `x, y` because the
reference benchmark is 2D. Production APIs should preserve the current
coordinate and schema model, and may provide 2D convenience overloads only when
they lower cleanly to `Coord3{ x, y, 0 }`.

## Hypothesis

The tile-map library will perform better and be easier to use if it defaults to row-major or row-major-in-chunk storage, then layers derived acceleration data on top:

```text
bitsets
chunk summaries
span emitters
halo/boundary caches
region graphs
dirty chunk lists
workload-specific iteration APIs
```

The expected win is not from finding one perfect physical layout. The expected win is exposing the right fast paths for common access patterns.

## Non-Goals

- Do not copy the benchmark driver into the library core.
- Do not make benchmark-only layout experiments part of the MVP API.
- Do not require users to understand space-filling curves for normal usage.
- Do not make Hilbert, Morton, Sierpinski, or Gosper ordering the default tile storage order.
- Do not optimize one benchmark at the cost of clear semantics, correctness, or maintainability.
- Do not treat current benchmark numbers as permanent truth.

## Design Decisions To Validate

### 1. Default Storage

The default canonical storage should be one of:

```text
row-major whole-map storage
row-major-in-chunk storage
SoA arrays using either of the above
```

Chunk interiors should remain locally stride-friendly.

Full curve ordering inside chunks is excluded from the MVP unless later benchmark evidence shows broad, repeatable wins across relevant workloads.

The current `LocalTileId` mapping is row-major inside a chunk:

```text
local_id = x + y * chunk.x + z * chunk.x * chunk.y
```

That is the baseline to preserve unless a future layout experiment explicitly
passes the promotion criteria below.

### 2. Layouts Are Backends, Not Gameplay Concepts

The public API should be expressed in map, chunk, tile, span, and query terms. Users should not need to know physical index order.

Required API direction, adapted to the existing 3D design:

```cpp
world.field<Tag>(Coord3{x, y, z})
world.try_field<Tag>(Coord3{x, y, z})
world.chunk(ChunkKey{key})
world.try_chunk(ChunkCoord3{x, y, z})
for_each_box_span<Shape>(Box3{origin, extent}, fn)
for_each_radius_span<Shape>(Coord3{cx, cy, cz}, radius, fn)
for_each_chunk(world, chunk_domain(keys), policy, fn)
```

The names above are illustrative. Existing names such as `field`, `chunk`,
`try_chunk`, `ChunkDomain`, and `for_each_chunk` should not be renamed merely to
match benchmark terminology.

Avoid exposing layout-specific concepts in normal gameplay code.

### 3. First-Class Span and Chunk Iteration

The library should support efficient span-based iteration for rectangular and radius-like queries.

Required concepts:

```text
rect spans
exact radius spans
chunk-local spans
interior spans
border spans
canonical tile count
visited tile count
rejected tile count
```

The exact interface is TBD, but the library must make dense iteration over contiguous chunk-local ranges easy.

Span APIs should be introduced as additions to the existing chunk-domain API,
not as replacements for `ChunkView` or `field_span<Tag>()`. A span emitter may
return world-coordinate spans, chunk-local spans, or both, but it must make the
coordinate space explicit.

### 4. Predicate Bitsets

Frequently queried boolean predicates should support bit-packed storage.

Candidate predicates:

```text
walkable
opaque
occupied
reserved
dirty
visible
has_object
has_job_provider
has_resource_kind
```

Bitsets may be authoritative only for derived predicates. Authoritative tile fields remain in normal typed storage unless explicitly designed otherwise.

### 5. Chunk Summaries

Each chunk may maintain summaries for selected predicates or layers:

```text
any
all
count
version
bounds of non-empty cells
optional density estimate
```

Chunk summaries are expected to help sparse queries and broad-phase rejection. They are not expected to help dense predicates where almost every chunk matches.

### 6. Halo / Boundary Caches

Persistent halos or boundary caches may be used for derived fields where dense neighbor access dominates.

Halo storage must be treated as non-canonical derived data.

It must define:

```text
canonical tile count vs storage size
halo update rules
dirty propagation across chunk boundaries
serialization behavior
thread-safety constraints
GPU upload behavior
debug validation
```

Halos are not part of the initial authoritative tile storage API.

### 7. Workload Taxonomy

The benchmark repo's workload categories should inform the library's performance model. The library should define explicit support for:

```text
span scans
rect/radius queries
dense stencils
sparse scatter updates
frontier traversal
line walks
broad-phase predicate search
region/path hierarchy queries
render/cache rebuilds
```

The benchmark repo remains the detailed reference for workload definitions and evolving measurements.

## Required Library Interfaces

### Layout Concept

The production layout concept should be smaller than the benchmark layout concept, but must preserve the useful parts.

Minimum candidate shape, generalized to the existing 3D coordinate model:

```cpp
struct Layout {
    using index_type = std::uint64_t;

    index_type index(LocalCoord3 coord) const;
    LocalCoord3 coord(index_type i) const;
    std::uint64_t canonical_tile_count() const;
    std::uint64_t storage_size() const;

    static constexpr bool chunked = /* ... */;
    static constexpr Extent3 chunk_extent = /* ... */;
};
```

`storage_size()` may exceed `canonical_tile_count()` for derived halo layouts, but canonical authoritative layouts should normally match.

The layout concept should describe local indexing and storage accounting. It
should not replace `ShapeTraits`, `TileKey`, `ChunkKey`, or `LocalTileId`
without a separate migration decision.

### Span Query API

The library should provide span emitters that avoid per-tile callback overhead when possible.

Candidate shape:

```cpp
struct TileSpan {
    Coord3 origin;
    std::uint32_t x_count;
};

for_each_box_span(Box3 box, fn);
for_each_radius_span(Coord3 center, std::uint32_t radius, fn);
for_each_chunk_span(ChunkKey chunk, Box3 local_box, fn);
```

For 2D top-down maps, `z` is usually constant and `x_count` represents a
contiguous row. For 3D maps, the first promoted span API must define whether a
span may cross z slices. The conservative default is one contiguous x-run at a
single y and z.

Span APIs must have simple reference implementations and cross-layout equivalence tests.

### Chunk Summary API

Candidate shape:

```cpp
struct ChunkSummary {
    bool any(PredicateId predicate) const;
    bool all(PredicateId predicate) const;
    std::uint32_t count(PredicateId predicate) const;
    std::uint64_t version(PredicateId predicate) const;
};
```

The exact representation is internal. Public APIs should expose semantics, not storage details.

### Predicate Bitset API

Candidate shape:

```cpp
PredicateView world.predicate(PredicateId id) const;
MutablePredicateView world.mutable_predicate(PredicateId id);
```

Predicate identifiers should not conflict with existing field tags. Derived
predicate storage may be keyed by explicit predicate ids, field tags, or a
schema extension, but authoritative tile data remains in typed fields unless a
later TDD changes that rule.

A predicate view should support:

```text
test tile
set/clear tile
iterate set bits in rect
iterate set bits in chunk
count in rect or chunk when supported
```

## Correctness Requirements

### Cross-Layout Equivalence

Every query must match a simple row-major reference implementation.

Success criterion:

```text
For every supported layout backend, 100,000 randomized rect/radius/line/frontier queries produce identical tile sets to the reference implementation.
```

### Span Equivalence

Span emitters must visit exactly the same canonical tiles as per-tile reference queries.

Success criterion:

```text
Span-based rect and radius queries match reference tile sets for all tested map sizes, chunk sizes, edge cases, and random seeds.
```

### Derived Data Consistency

Bitsets, summaries, halos, and region caches must match authoritative tile state after explicit maintenance or flush.

Success criterion:

```text
After randomized mutation sequences followed by maintenance flush, all derived structures validate against authoritative storage for 10,000 seeds.
```

### Halo Non-Canonical Safety

Halo cells must never be exposed as canonical map tiles.

Success criterion:

```text
Public iteration APIs never return halo-only cells, and debug validation detects intentional halo corruption or stale halo boundaries.
```

### Serialization Safety

Serialized worlds must not persist stale derived data unless the format explicitly marks it as rebuildable cache data.

Success criterion:

```text
Load(save(world)) produces identical authoritative state and either identical validated derived state or correctly rebuilt derived state.
```

## Benchmark Requirements

Benchmarks should remain separate from library correctness tests. They should be runnable in CI and locally, but should not make normal builds slow.

Required benchmark groups:

```text
rect scan
radius query
dense stencil
predicate scan
hierarchical sparse predicate search
frontier/path-like expansion
line walk
chunk dirty rebuild
halo maintenance
```

Each benchmark should record at least:

```text
layout/backend
map size
chunk size
query size or radius
predicate density when applicable
visited canonical tiles
visited storage cells
rejected tiles
number of spans
number of chunks touched
elapsed time
median
p95
```

The benchmark repo may define additional metrics. Do not duplicate all of them here.

## Success Criteria For Integration

A benchmark-inspired technique may be promoted into the library only if all applicable criteria pass.

### Default Storage Promotion

Row-major-in-chunk becomes the preferred default if:

```text
It is within 10% of whole-map row-major on dense scans and simple stencils.
It is faster or more ergonomic for chunk-local maintenance and dirty chunk workflows.
It does not complicate public APIs.
It passes all cross-layout equivalence tests.
```

### Exotic Layout Promotion

A curve-based layout may be included as an optional backend only if:

```text
It beats row-major-in-chunk by at least 20% on at least two important workload groups.
It is not worse by more than 10% on dense scans, stencils, and common radius queries.
It has clear documentation explaining when to use it.
It does not leak layout-specific behavior into normal APIs.
```

A curve-based layout may become default only if:

```text
It beats row-major-in-chunk across the weighted benchmark suite on at least three target CPU families.
It has no major pathological losses on MVP workloads.
It preserves simple chunk-local iteration.
```

This is expected to be unlikely.

### Predicate Bitset Promotion

Predicate bitsets should be promoted into the core library if:

```text
They improve full-map predicate scans by at least 4x for common boolean predicates.
They improve sparse predicate search or broad-phase rejection by at least 2x when density is below the documented threshold.
Mutation overhead remains below 10% for normal tile writes involving maintained predicates.
Memory overhead is documented and acceptable.
```

### Chunk Summary Promotion

Chunk summaries should be promoted into the core library if:

```text
They improve sparse broad-phase queries by at least 2x at low predicate density.
They do not regress dense queries by more than 10% when bypassed or quickly rejected as unhelpful.
They can be updated incrementally or rebuilt during chunk maintenance.
They have deterministic validation against authoritative state.
```

### Halo / Boundary Cache Promotion

Halo caches should be promoted as an experimental backend if:

```text
They improve dense stencil workloads by at least 25% on at least two target CPU families.
They do not introduce stale-boundary bugs under randomized mutation and maintenance tests.
They provide clear canonical-vs-storage addressing rules.
They can be rebuilt or invalidated cheaply enough for dynamic maps.
```

Halo caches should not become default until multiple real game-like workloads benefit.

### Span API Promotion

Span query APIs should be promoted into the public API if:

```text
They reduce callback or index overhead by at least 15% for rect/radius scans.
They remain simple enough to use from game code.
They can be implemented correctly for all MVP layouts.
They provide obvious debug/reference fallbacks.
```

## Integration Plan

### Phase 1: Mirror Benchmark Concepts, Not Code

- Add layout concept tests to the library.
- Add row-major and row-major-in-chunk reference backends if they are useful
  for equivalence tests.
- Add reference rect, radius, and line queries.
- Add span emitters behind an experimental namespace.
- Add benchmark harness hooks that can compare against the external benchmark repo methodology.

### Phase 2: Add Derived Acceleration Structures

- Add predicate bitsets for selected derived predicates.
- Add per-chunk summaries for sparse broad-phase queries.
- Reuse existing chunk dirty flags and version counters where their semantics
  fit; add new metadata only when the current `ChunkMeta` model is insufficient.
- Add validation against authoritative tile state.

### Phase 3: Evaluate Halo Caches

- Prototype halo storage for one derived field.
- Restrict usage to dense stencil workloads.
- Validate boundary updates under randomized mutation.
- Benchmark against non-halo chunk-local kernels.

### Phase 4: Decide On Optional Layout Backends

- Keep curve-based layouts in benchmarks or experimental namespaces unless success criteria pass.
- Prefer chunk-order experiments over curve-ordering every tile.
- Document any promoted layout with clear workload guidance.

## Documentation Updates

Update or add the following design docs:

```text
TDD: Layout Interface and Storage Backends
TDD: Chunk Iteration and Span Queries
TDD: Predicate Bitsets and Chunk Summaries
TDD: Halo / Boundary Cache
TDD: Benchmark and Regression Suite
TDD: Derived Data and Dirty Maintenance
```

Each doc should link to the benchmark repo only for detailed evidence and evolving measurements. The tile-map library docs must still define their own API, semantics, and success criteria.

## Risks

### Overfitting To Synthetic Benchmarks

Mitigation:

```text
Use the benchmark repo as evidence, but require game-like scenarios before promoting complex features.
```

### API Pollution From Layout Experiments

Mitigation:

```text
Keep experimental layouts behind backend interfaces and namespaces.
Do not expose curve-specific concepts in ordinary gameplay APIs.
```

### Derived Data Drift

Mitigation:

```text
Use version counters, debug validation, randomized mutation tests, and explicit flush points.
```

### Memory Growth

Mitigation:

```text
Track memory overhead for bitsets, summaries, halos, and caches separately from canonical tile state.
Require storage_size() and canonical_tile_count() reporting.
```

### Hidden Determinism Bugs

Mitigation:

```text
Authoritative simulation state remains separate from async derived maintenance.
Derived outputs are deterministic at explicit flush points.
```

## Open Questions

- Which predicates should be maintained as bitsets by default?
- Should chunk summaries be always-on, opt-in per layer, or generated by query planners?
- What chunk sizes should be supported in the MVP?
- Should halo caches be per-field, per-layer, or per-kernel?
- How should GPU-oriented layouts interact with CPU-oriented canonical storage?
- What target machines define success for the weighted benchmark suite?

## Decision Summary

Adopt the benchmark repo's main architectural lesson:

```text
Boring canonical storage first.
Fast chunk/span APIs second.
Derived acceleration structures third.
Exotic physical layouts last, and only with evidence.
```

This addendum should guide library design toward practical, workload-driven APIs without turning the library into a copy of the benchmark repository.
