# Span Queries

`include/tess/query/span.h` provides exact, allocation-free spatial span
emitters. A `TileSpan` is one contiguous x-axis run at a single y and z; spans
never cross rows or z slices.

## Public Surface

- `shape_bounds<Shape>()` returns the canonical half-open world box.
- `for_each_box_span<Shape>(box, fn)` clips a half-open box to the shape and
  emits runs in stable z, y, x order.
- `for_each_radius_span<Shape>(center, radius, fn)` emits the exact inclusive
  Euclidean lattice ball, clipped to the shape. Integer square-root correction
  avoids floating-point boundary ambiguity.
- `for_each_chunk_span<Shape>(chunk, local_box, fn)` clips a chunk-local box
  and reports canonical world coordinates.
- `SpanQueryStats` reports emitted spans and represented tiles.

The span count is 32-bit. A wider logical row is split into adjacent spans
without dropping tiles. Coordinate arithmetic is checked and bounded by the
shape constraints, including shapes whose axes exceed 32 bits.

Reference tests compare 100,000 seeded box and radius queries across top-down,
vertical, and 3D shapes. The warm emitter path performs no dynamic allocation.
Local five-repetition medians for 512-wide queries measured 678 ns versus
213,076 ns for per-tile rectangular callbacks and 1,789 ns versus 157,203 ns
for radius scanning. Both exceed the historical 15% promotion gate. These
numbers are decision evidence, not portable performance ceilings.

Predicate bitsets and chunk summaries are not part of this surface. They need
authoritative predicate identity, mutation-cost evidence, and their separate
4x/2x historical promotion gates before introduction.
