# tess_block_test

- `tess_block_test`: verifies chunk-domain builders, policy-typed `BlockCtx`
  construction and iteration, serial block iteration, owned domain lifetimes,
  const-correct chunk views and world access including compile-time and runtime
  `ReadOnly` policy enforcement, runtime dispatch across every `WritePolicy`
  value (including mutable views for `UniquePerTile`, `UniquePerChunk`, and
  `Unsafe`), runtime invalid write-policy fail-fast
  behavior, chunk bounds for 2D vertical and 3D worlds, chunk-local tile
  iteration and coordinate helpers,
  boundary/local-candidate helpers across 2D/3D and degenerate axes, and
  allocation-free iteration for prebuilt domains and contexts, including
  pre-reserved caller-owned block scratch and explicit scratch-exhaustion
  diagnostics. `BlockScratch` coverage pins zero-count allocations,
  byte-count overflow rejection (no wrapped tiny allocations), mixed
  char/`std::uint64_t`/max-aligned allocations staying aligned and
  disjoint, and growth keeping `used_bytes()` while serving new
  allocations from fresh storage.
