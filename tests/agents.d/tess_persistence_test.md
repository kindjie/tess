# tess_persistence_test

- `tess_persistence_test`: verifies the canonical little-endian world archive
  envelope against a full golden byte fixture, dense and sparse authoritative-
  field round trips, canonical sparse chunk order, every envelope/key/
  residency/chunk compatibility status, explicit migration-required outcomes,
  scoped-enum support with representable unknown values, compile-time rejection
  of unscoped enums, complete scalar preflight and corruption/truncation
  rejection without target mutation, full-archive checksum coverage including
  metadata, short invalid magic, future-version classification before v1
  framing, duplicate field-id rejection during inspection, invalid descriptor
  kind/width domains, excess field counts, out-of-range chunk keys, short
  headers and inconsistent body lengths, exact NaN/negative-zero bit-pattern
  preservation, dense logical chunk-count completeness,
  short-circuit field decoding after the first invalid scalar, direct
  dense/sparse canonical-key ordering checks, sparse-capacity preflight, and
  dense-version or sparse-generation invalidation on load.
