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
  statuses are `Planned`, `InvalidWritePolicy`, `InvalidDomain`,
  `InvalidFieldAccess`, and `HazardConflict`.
- `OperationFailure` records the stable reason for an invalid operation. The
  current reasons are `InvalidWritePolicyValue` and
  `ExplicitChunkOutOfRange`, `ReadOnlyWriteMask`, and
  `FieldHazardConflict`.
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
- `ExecutionPhasePlan` stores deterministic contiguous ranges of planned
  operations that are eligible to run in the same future parallel phase.
  `plan_parallel_execution_phases(plan)` accepts only `ReadOnly` and
  `UniquePerChunk` planned operations for now. If a mutable chunk operation
  touches a chunk already present in the current phase, the planner starts a
  later phase so chunk field data and dirty/version metadata are not mutated
  concurrently. `UniquePerTile` is deliberately rejected until tile subdomains
  and tile-level ownership validation exist.
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
- `PlannedDirtyAccumulator` records `{chunk, dirty_mask, bounds}` dirty events
  produced during planned execution without mutating chunk metadata.
  `merge_planned_dirty(world, accumulator)` sorts records by chunk key,
  coalesces repeated chunk records into one dirty mask and unioned bounds,
  applies `world.mark_dirty` once per touched chunk, clears the accumulator,
  and returns the number of merged chunks.

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
- deterministic field hazards against earlier successfully planned operations

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
Hazard reports include the earlier conflicting operation handle/id and the
overlapping field mask. The planner does not reorder operations or insert
barriers; it rejects the later conflicting operation and keeps the earlier
planned operation.

`plan_parallel_execution_phases(plan)` is a separate conservative view over a
successful plan. It does not execute work and does not relax existing hazard
validation. It groups already-planned operations into deterministic phase
ranges that preserve operation order. Read-only operations can share a phase
with other read-only operations. `UniquePerChunk` operations can share a phase
only when their chunk domains are disjoint from other mutable work in that
phase. Same-chunk mutable work is separated even if field masks are disjoint,
because chunk dirty bounds and version metadata are shared at chunk scope.

The plan-to-block adapter is intentionally non-owning. The planned operation
must outlive any `ChunkDomain` or `BlockCtx` produced from it, because those
objects point at `planned.chunks`. Adapter construction and iteration over a
prebuilt plan do not allocate.

## Minimal Execution Bridge

Planned operations can now be executed explicitly through the serial block API:

```cpp
auto result = tess::execute_planned_operation<
    tess::WritePolicy::UniquePerChunk>(world, planned, [](auto view) {
  auto terrain = view.template field_span<TerrainTag>();
  terrain[0] = 1;
});
```

`execute_planned_operation<Policy>` rejects policy mismatches before invoking
the callback. On success, it creates a policy-typed `BlockCtx`, visits the
planned chunks in deterministic key order, invokes the callback once per chunk
view, and marks each visited chunk dirty when the planned operation declares a
nonzero `dirty_mask`.

`execute_plan<Policy>` applies the same callback to each planned operation in
plan order and stops at the first policy mismatch. This is still a synchronous
caller-driven bridge, not a scheduler.

`execute_planned_operation_deferred_dirty<Policy>` and
`execute_plan_deferred_dirty<Policy>` run the same callbacks but record
declared dirty chunks into a caller-owned `PlannedDirtyAccumulator` instead of
mutating world metadata inside the callback loop. Callers can then merge dirty
metadata after a serial operation, after a whole phase, or after future worker
jobs complete. The deferred path is the intended handoff point for parallel
execution because workers can keep dirty records local and the main thread can
merge metadata deterministically.

`PlannedDirtyPartitions` owns multiple dirty accumulators and
`PlannedPhaseExecutionScratch` owns per-operation dirty partitions,
per-operation execution results, and a merged-dirty scratch accumulator. These
types let a phase backend route each planned operation into a distinct dirty
buffer instead of sharing one `PlannedDirtyAccumulator` across callbacks.
`collect_planned_dirty(...)` appends partition records into a caller-owned
accumulator and clears the partitions. `merge_planned_dirty(world, partitions,
scratch)` and `merge_planned_dirty(world, phase_scratch)` collect those records,
sort/coalesce by chunk key through the normal dirty merge, update world
metadata, and clear the intermediate buffers.

`execute_phase_deferred_dirty<Policy>` executes one `ExecutionPhase` range from
an `ExecutionPlan` through the same deferred-dirty path. It validates the phase
range and policy before invoking callbacks, visits only operations in that
range, and returns `InvalidPhase` or `PolicyMismatch` without side effects when
the phase cannot be executed. The default implementation is serial; future
worker backends should preserve the same validation and dirty-merge contract.

`ExecutorPhaseRange` is the backend-facing operation-index range shape copied
from an `ExecutionPhase` by `executor_phase_range(phase)`.
`execute_operation_index_range(executor, range, fn)` adapts that range to the
current operation-index executor contract. `SerialPhaseExecutor` is the default
executor used by `execute_phase_deferred_dirty<Policy>`. Callers that need to
test a backend integration point can use
`execute_phase_deferred_dirty_with<Policy>` and pass an executor that provides:

```cpp
auto for_each_operation(std::size_t first, std::size_t count, Fn&& fn)
    -> tess::PlannedExecutionResult;
```

The executor receives planned operation indexes and must return the first
non-`Executed` result from the callback, or `Executed` if the whole range
completed. This helper is still a serial-safe bridge over one caller-owned
`PlannedDirtyAccumulator`; custom executors must serialize callback invocation
or provide their own synchronization. Any executor used with these helpers must
complete, join, or otherwise make all invoked callbacks visible before
returning its `PlannedExecutionResult`; cancellation and partial completion are
not modeled yet.

`ScopedThreadPhaseExecutor` is the first production executor prototype for this
contract. It owns no persistent pool: each call splits one operation-index
range across a bounded number of `std::thread` workers, joins all workers
before returning, and reports the first non-`Executed` callback result in
operation order. It exists to prove the phase handoff and visibility rules
before Tess adds a long-lived worker pool.

`execute_phase_partitioned_dirty_with<Policy>` uses the same executor contract,
but stores callback dirty records and execution results in
`PlannedPhaseExecutionScratch` by operation offset. This avoids shared result
and dirty-buffer mutation during dispatch; after the executor returns, the
helper reduces operation results in plan order and callers merge dirty metadata
deterministically with `merge_planned_dirty(world, scratch)`. The scratch can be
pre-reserved and prepared for a known phase operation count so warm partitioned
execution and dirty merge do not allocate. Queued-operation tests exercise this
contract with a test-only `std::thread` executor over disjoint chunk mutations
and overlapping read-only chunk access. The ownership rule is deliberately
small: `ReadOnly` phase operations may overlap chunk domains and receive const
views, while `UniquePerChunk` phase operations may run concurrently only after
phase planning proves their mutable chunk domains are disjoint. User callbacks
must still synchronize any mutable state they capture themselves. Production
worker pools, cancellation, and result-channel completion remain outside this
layer.

Partitioned execution has no cancellation semantics yet. If one operation fails
policy validation while a parallel executor has already started other
operations, those operations may still complete before the executor returns.
`execute_phase_partitioned_dirty_with<Policy>` reports the first non-`Executed`
operation result in plan order and includes only earlier successful chunks in
the returned `chunk_count`; completed dirty partitions remain in caller-owned
scratch. The caller decides whether to merge or discard those dirty records
after a failure.

Inspecting existing queued operations, reports, and planned operations returns
non-owning spans and does not allocate. Enqueueing and domain/report expansion
may allocate because `FrameOps`, explicit domains, reports, and planned chunk
lists own their storage.

## Deliberate Limits

This slice is a planner scaffold only. It intentionally does not implement:

- queued kernel storage, executor integration, or result channels
- callback ownership, queued kernel storage, result channels, or async handles
- persistent worker scheduling, worker pools, async work, or result-channel
  completion
- automatic execution of planned operations through a scheduler
- thread-local dirty accumulator policy beyond caller-owned per-operation
  phase partitions
- topology, pathfinding, movement, residency transitions, or GPU selection
- work-contract or maintenance-scheduler semantics
- field tag reflection, typed read/write sets, or automatic dirty-flag
  mutation
- hazard analysis beyond deterministic same-plan field-mask conflicts over
  overlapping chunk domains
- sparse residency or non-resident chunk loading
- rich diagnostics beyond per-op status, failure reason, limited detail, access
  metadata, and captured source location

The Work Contracts addendum remains an experiment proposal. Current queued ops
use the existing dirty/active metadata scans as the baseline and do not add
coalescing scheduler handles or long-lived maintenance tasks.

The phase executor contract is deliberately library-agnostic. External
backends must adapt to the contiguous operation-index range API and preserve
the documented completion, visibility, dirty-partition, and result-reduction
rules before they are wired into queued operations.

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
  location plus limited diagnostics, not backend choice, solved/reordered
  hazards, versions, or result channels.
- The plan-to-block adapter only exposes successful planned chunk domains to
  the existing serial block API. It does not execute queued intent by itself.

Those omissions are intentional so future scheduler, topology, pathing, and
diagnostics slices can be added against a deterministic queue and domain
foundation.
