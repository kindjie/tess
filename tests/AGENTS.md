# Tests

- Tests use GoogleTest.
- `tess_smoke`: verifies that the public `tess::tess` target is consumable,
  that the root public header compiles, and that public version constants match.
- `tess_shape_test`: verifies public shape primitives, constexpr shape traits,
  degenerate-axis handling, containment helpers, key width inference, and
  coordinate/chunk/local/tile key conversion helpers.
- `tess_storage_test`: verifies typed field schemas and resident chunk pages,
  including SoA field independence, contiguous typed spans, metadata, const
  access, and allocation-free local field/span access.
