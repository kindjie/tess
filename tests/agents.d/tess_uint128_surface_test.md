# tess_uint128_surface_test

- `tess_uint128_surface_test`: pins `UInt128`'s operator set in both
  directions. The supported operations must compile (implicit construction
  from an unsigned integer, comparison, multiply/subtract/bitwise/shift,
  explicit narrowing) and the unsupported ones must not (addition,
  division, modulo, increment, implicit narrowing). The type is a bit
  carrier for packed tile keys, not a general 128-bit integer, and a
  comment saying so does not stop the surface growing one convenience
  operator at a time — this makes widening it an edit to
  `tests/tess_uint128_surface_test.cc`.
