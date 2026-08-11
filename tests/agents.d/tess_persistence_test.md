# tess_persistence_test

- `tess_persistence_test`: pins the canonical little-endian archive bytes,
  dense and sparse round trips, compatibility classification, validation, and
  failure atomicity. Corruption or insufficient sparse capacity must be
  rejected before target mutation; floating-point payloads preserve NaN and
  negative-zero bit patterns exactly.
