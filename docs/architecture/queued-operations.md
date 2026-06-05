# Queued Operations Foundation

The current queued-operations layer is the first M4 scaffold over the existing
storage and block APIs. It lives in `include/tess/ops/queued.h` and is exported
by `tess/tess.h`.

## Public Surface

- `FrameOps` owns the operations submitted for one planning frame.
- `OpHandle` is a stable handle assigned when an operation is enqueued.
- `OpId` is assigned in enqueue order. The current handle value and id value
  both start at zero and advance together, but they remain separate public
  types.
- `Priority` records broad planning priority: `Immediate`,
  `GameplayCritical`, `VisibleSoon`, `Background`, and `Maintenance`.
- `BudgetPolicy` records basic budget intent: `MustRun`, `CanDefer`,
  `CanSkipIfSuperseded`, and `BudgetedIncremental`.
- `OperationStatus` records per-operation planner outcome. The current
  statuses are `Planned`, `InvalidWritePolicy`, and `InvalidDomain`.
- `OperationFailure` records the stable reason for an invalid operation. The
  current reasons are `InvalidWritePolicyValue` and
  `ExplicitChunkOutOfRange`.
- `DomainDesc` owns a minimal operation domain descriptor:
  `explicit_chunks(keys)`, `dirty_chunks(mask)`, `active_chunks(mask)`, or
  `resident_chunks()`.
- `FieldAccessDesc` records untyped field access masks for an operation:
  `read_mask`, `write_mask`, and `dirty_mask`. These are planner metadata only
  in this slice; they do not mutate world dirty flags and are not field-tag
  reflection.
- `QueuedOperation` stores the submitted operation kind, handle, id, domain,
  field access, `WritePolicy`, priority, budget policy, and
  `std::source_location`.
- `OperationAccess` records the diagnostic access metadata known to the
  planner: write policy, domain kind, and domain mask. It is diagnostic
  metadata only in this slice, not a hazard solver.
- `ExecutionPlan` stores planned operations in enqueue order. Each planned
  operation contains the diagnostic access metadata and expanded chunk-key
  vector.
- `ExecutionReport` stores one report entry per queued operation and the plan
  entries for operations that passed validation.
- `planned_chunk_domain(planned)` adapts a successful planned operation into a
  non-owning `ChunkDomain` span over the planned operation's owned chunk
  vector.
- `planned_policy_matches<Policy>(planned)` checks whether the planned write
  policy exactly matches the requested block policy.
- `try_planned_block_ctx<Policy>(world, planned)` returns a policy-typed
  `BlockCtx` over the planned chunk domain when the policies match, or
  `std::nullopt` on mismatch.

The first submitted operation category is `FrameOps::update_field(...)`. It
records field/block-style work intent only; it does not accept callbacks or
invoke kernels.

## Planning Behavior

`plan_operations(world, ops)` validates and expands queued operations over the
current always-resident world metadata.

Validation currently covers:

- invalid `WritePolicy` enum values
- explicit chunk keys outside the world
- `ReadOnly` operations that declare nonzero field write masks

Expansion currently covers:

- explicit chunks copied into deterministic ascending `ChunkKey` order
- dirty chunks discovered through `world.dirty_chunks(mask)`
- active chunks discovered through `world.active_chunks(mask)`
- all chunks in an always-resident world for `resident_chunks()`

Planning preserves enqueue order for reports and successful plan entries.
Operations with invalid write policies or invalid domains still receive report
entries, but they do not produce plan entries.

Report helpers expose deterministic inspection without changing ownership:

- `ok()` and `failed()` summarize whether any operation failed validation.
- `planned_count()` returns the number of successful plan entries.
- `failed_count()` returns the number of failed report entries.
- `find(handle)` performs a linear lookup in report order and returns the
  matching report entry or `nullptr`.

Invalid operation reports include `OperationFailure`. Invalid explicit chunk
domains also include the first out-of-range `ChunkKey` as diagnostic detail.
Invalid read-only write-mask reports include the submitted `FieldAccessDesc` so
callers can diagnose the bad declaration.

The plan-to-block adapter is intentionally non-owning. The planned operation
must outlive any `ChunkDomain` or `BlockCtx` produced from it, because those
objects point at `planned.chunks`. Adapter construction and iteration over a
prebuilt plan do not allocate.

Inspecting existing queued operations, reports, and planned operations returns
non-owning spans and does not allocate. Enqueueing and domain/report expansion
may allocate because `FrameOps`, explicit domains, reports, and planned chunk
lists own their storage.

## Deliberate Limits

This slice is a planner scaffold only. It intentionally does not implement:

- callbacks, kernel invocation, executor integration, or result channels
- barrier insertion, batching heuristics, async work, or worker scheduling
- automatic execution of planned operations
- topology, pathfinding, movement, residency transitions, or GPU selection
- work-contract or maintenance-scheduler semantics
- field tag reflection, typed read/write sets, or automatic dirty-flag
  mutation
- hazard analysis beyond recording diagnostic access metadata, validating the
  current `WritePolicy` enum value, and rejecting read-only write masks
- sparse residency or non-resident chunk loading
- rich diagnostics beyond per-op status, failure reason, limited detail, access
  metadata, and captured source location

The Work Contracts addendum remains an experiment proposal. Current queued ops
use the existing dirty/active metadata scans as the baseline and do not add
coalescing scheduler handles or long-lived maintenance tasks.

## TDD Divergences

The historical queued-operations TDD describes a much larger public planning
and execution system. This M4 slice keeps only the stable foundation needed by
later work:

- `FrameOps::update_field(...)` records intent and optional untyped field
  access masks without a kernel type.
- `DomainDesc` only supports chunk-domain descriptors that can be resolved by
  current always-resident storage.
- `ExecutionPlan` is only an ordered list of expanded chunk keys per operation,
  not a phase graph.
- `ExecutionReport` reports validation status, chunk count, and source
  location plus limited diagnostics, not backend choice, solved hazards,
  versions, or result channels.
- The plan-to-block adapter only exposes successful planned chunk domains to
  the existing serial block API. It does not execute queued intent by itself.

Those omissions are intentional so future scheduler, topology, pathing, and
diagnostics slices can be added against a deterministic queue and domain
foundation.
