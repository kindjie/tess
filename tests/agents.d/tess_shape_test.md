# tess_shape_test

- `tess_shape_test`: verifies public shape primitives, constexpr shape traits,
  default and explicit lattice typing with stable lattice identifiers,
  axial hex coordinate conversion and overflow-safe saturated distance,
  degenerate-axis handling, containment helpers, key width inference,
  coordinate/chunk/local/tile key conversion helpers, the portable
  `tess::UInt128` operations (carrying multiply, borrow subtract,
  boundary shifts including counts of 64/127/>=128, comparisons, narrowing,
  non-negative int construction, a death test for the negative-int
  constructor precondition when asserts are enabled,
  and `bit_width`/`bits_for_count` at the 64-bit boundary), >64-bit
  tile-key round-trips on the huge bounded shape, and the largest legal
  64-bit boundary shape (single chunk, 2^63 local tiles, `chunk_bits == 0`)
  round-tripping without wide shifts.
