# Tests

- Tests use GoogleTest.
- `tess_smoke`: verifies that the public `tess::tess` target is consumable,
  that the root public header compiles, and that public version constants match.
- `tess_shape_test`: verifies public shape primitives, constexpr shape traits,
  degenerate-axis handling, containment helpers, key width inference, and
  coordinate/chunk/local/tile key conversion helpers.
- `tess_storage_test`: verifies typed field schemas, resident chunk pages, and
  always-resident dense worlds, including SoA field independence, contiguous
  typed spans, metadata, const access, key/coord lookup, coordinate resolution,
  checked invalid-coordinate behavior, per-chunk dirty/active metadata,
  noexcept hot accessors, and allocation-free local field/span/world access
  after construction.
- `tess_block_test`: verifies chunk-domain builders, policy-typed `BlockCtx`
  construction and iteration, serial block iteration, const-correct chunk
  views including `ReadOnly` policy enforcement, chunk bounds for 2D vertical
  and 3D worlds, chunk-local tile iteration and coordinate helpers,
  boundary/local-candidate helpers across 2D/3D and degenerate axes, and
  allocation-free iteration for prebuilt domains and contexts.
