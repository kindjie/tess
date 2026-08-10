# tess_queued_property_test

- `tess_queued_property_test`: seeded planning sequences over queued
  operations (redesign section 3.4, phase 7 slice b-i). Randomizes all
  nine `OperationKind` values against every write policy, four
  field-access patterns, and overlapping/disjoint chunk domains, then
  replans the whole batch after every enqueue so an invariant that only
  breaks for a particular ORDER of kinds and policies is exercised.
  Before this, the suite enqueued `update_field` at 90 call sites and
  `mark_dirty` at four, and all nine kinds appeared together in exactly
  one test, once.

  The headline property is that **planning decisions and diagnostics
  ignore the operation kind**: the planner never reads `op.kind` and
  the report does not carry it, so rewriting every operation's kind
  must not change any outcome. The model asserts this by copying the
  queued operations, rewriting only that field, replanning, and
  comparing every report field except the source location plus every
  planned-operation field except the kind — which IS deliberately
  preserved, and is asserted to be. Comparing only report rows would
  miss a planner that used the kind to alter what an accepted operation
  carries into execution. Mutation-verified.

  It rewrites to `MarkDirty` as well as `BuildFieldProduct` for a
  specific reason: `FrameOps::mark_dirty` synthesizes its own metadata
  (read-only, mask copied into `dirty_mask` and invalidations), so the
  generated policy and read/write masks are discarded for that kind.
  The alphabet's kind x policy x access product is therefore not real
  for `MarkDirty`, and the rewrite is the only route by which the
  planner ever sees one carrying arbitrary metadata.

  Three claims here are deliberately narrower than they look, because
  the wider version is false. Phases partition the plan contiguously
  ONLY when the phase plan is ok: parallel phases support just
  `ReadOnly` and `UniquePerChunk`, so any `UniquePerTile` or `Unsafe`
  operation stops planning part-way and the phases built so far are a
  PREFIX. Only a `HazardConflict` row carries conflict diagnostics.
  And handle/id density is a `FrameOps` guarantee — the span overload
  deliberately accepts arbitrary operations and rejects non-dense
  identity instead.

  `TheSweepReachesEveryPlanningOutcomeItCanReach` gates all of it: the
  sweep must produce a hazard conflict, a multi-phase plan, an
  unsupported-policy prefix, all nine kinds, and the invalid-policy,
  invalid-domain and invalid-field-access rejections. The alphabet
  carries field-access masks (a hazard needs intersecting masks), an
  out-of-range chunk key, an unsorted duplicate key set (so the
  descriptor's normalization is exercised rather than handed
  already-normal input), and one policy value outside the enum —
  without each of those the corresponding rule holds vacuously. The
  test name is qualified because `InvalidIdentity` is NOT reachable
  from `FrameOps`, which always assigns dense identities; the span
  overload's rejection of non-dense input is covered separately.
