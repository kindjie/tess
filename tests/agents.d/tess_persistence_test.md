# tess_persistence_test

- `tess_persistence_test`: pins the format-v2 16-byte chunk prefix and
  canonical little-endian archive bytes,
  dense and sparse round trips, compatibility classification, validation, and
  failure atomicity. Corruption or insufficient sparse capacity must be
  rejected before target mutation; floating-point payloads preserve NaN and
  negative-zero bit patterns exactly. Format v1 and unknown future formats
  return `UnsupportedFormat` before version-specific interpretation.
