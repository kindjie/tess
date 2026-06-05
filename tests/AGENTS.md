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
  allocation-free iteration for prebuilt domains and contexts, including
  pre-reserved caller-owned block scratch and explicit scratch-exhaustion
  diagnostics.
- `tess_queued_test`: verifies the M4 queued-operations scaffold, including
  empty-frame planning, stable handles and enqueue-order ids, explicit/dirty/
  active/resident chunk-domain expansion, enqueue-order plan preservation,
  diagnostic access metadata, untyped field access mask propagation,
  structured invalid write-policy, invalid field-access, and explicit-domain
  rejection, report lookup/count helpers, mixed valid/invalid report ordering,
  deterministic field-mask hazard validation, plan-to-block adapters, policy
  mismatch rejection, allocation-free prebuilt planned block iteration,
  explicit planned execution with dirty propagation, policy-mismatch execution
  rejection, allocation-free prebuilt planned execution, source-location
  capture, and allocation-free inspection of already-built queue/report/plan
  spans.
- `tess_path_test`: verifies the MVP A* path foundation, including top-down 2D
  paths around blocked tiles, invalid start and goal reporting, no-path
  reporting, direct-path and uniform-cost fast paths across top-down 2D,
  vertical 2D, and 3D layouts, coordinate support, shared-goal distance-field
  builds and reconstruction, mismatched-field rejection, and allocation-free
  repeated queries with pre-reserved path scratch.
- `tess_diagnostics_default_test`: verifies public diagnostic macros are
  disabled by default and do not evaluate arguments, including generic events.
- `tess_diagnostics_enabled_test`: verifies public diagnostic macros evaluate
  exactly once when `TESS_ENABLE_DIAGNOSTICS` is defined, and that scoped path
  counters record generic diagnostic events. It also links diagnostic
  allocation hooks and verifies scoped allocation counters observe global
  `new`/`delete`.
