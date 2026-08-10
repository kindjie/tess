- Three types that appeared in public signatures while living in
  `tess::detail` are now public, because `docs/style.md` says `detail`
  carries no source-compatibility guarantee and each of these was
  something a consumer had to name:
  - `UInt128` — `TileKey::value` is spelled with it for any shape needing
    more than 64 tile-key bits. `core/uint128.h` moves from the
    implementation header set to the public one accordingly. Shapes that
    fit in 64 bits keep a plain `std::uint64_t`, so most consumers never
    encounter it.
  - `PhaseDirtyPartition` — `PlannedPhaseExecutionScratch::dirty_partitions()`
    returns a span of it, so reading per-operation dirty records required
    naming a `detail` type.
  - `PibtRanking` — it constrains public PIBT entry points, so a caller
    whose ranking callable was rejected could not name the concept
    deciding it.
- `UInt128`'s implicit constructor from a signed integer is templated over
  `std::signed_integral` rather than taking a plain `int`. An `int`
  parameter accepted any wider signed value through a silent narrowing
  conversion, so `UInt128 v = std::int64_t{1} << 32` was **zero**. That
  was a latent defect while the type was internal; promoting it would have
  made it a public one.
- `UInt128`'s operator set stays deliberately partial and is now pinned by
  `tess_uint128_surface_test`, which asserts both that the supported
  operations compile and that addition, division, modulo, increment and
  implicit narrowing do not. It carries packed key bits; it is not a
  general 128-bit integer, and a comment alone would not have stopped the
  surface growing one convenience operator at a time.
- `integration-policy.md` gains a section on
  `include/tess/experimental/`: names there may change or be removed in
  any release and are excluded from a future 1.0 promise. Within it,
  `FifoScheduler` and `CoalescingScheduler` are the supported spellings,
  while the template they alias, `detail::QueuedScheduler`, deliberately
  is not — that indirection exists so the alias can be repointed without
  breaking callers. Promoting the template would have frozen an
  implementation detail of the layer explicitly labelled unstable.
- `PibtFrame` stays in `detail` and is no longer exposed at all: its one
  public appearance was `PibtPriorities::frames`, and that member is now
  private. Promoting the frame type would have frozen an implementation
  struct instead.
