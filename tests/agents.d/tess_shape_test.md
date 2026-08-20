# tess_shape_test

- `tess_shape_test`: pins public shape, lattice, coordinate, key-width, and
  portable `UInt128` primitives, with overflow and degenerate-axis boundaries.
  `Coord2` must remain a lossless implicit lift to the canonical z-zero
  `Coord3`, including inside aggregate `PathRequest` initialization.
  The largest legal 64-bit shape has one chunk and 2^63 local tiles, so
  `chunk_bits == 0`; its round trip must avoid a full-width shift.
