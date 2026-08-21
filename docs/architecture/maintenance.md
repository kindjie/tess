# Maintenance Scheduling

`include/tess/maintenance.h` and the narrow headers under
`include/tess/maintenance/` define the stable source contract for derived-state
maintenance. They do not alter world construction, authoritative storage,
exact event handling, or simulation command execution. Include the maintenance
aggregate explicitly: the compatibility umbrella `tess/tess.h` deliberately
does not import it. This keeps experimental implementation spellings and their
platform-specific scheduler machinery out of the dependency-free core
umbrella. Because the facade is an alias-only source move, its headers
transitively make those experimental spellings reachable; reachability does not
make the virtual scheduler or deferred backends stable.

The stable spellings share the exact pre-graduation implementation types. This
mechanical source move keeps the controlled M3 and Steam Deck campaign
representative; stability attaches to the documented `tess::maintenance`
names and semantics, not to undocumented implementation namespaces, object
layout, or a virtual ABI.

## Stable Surface

- `MaintenanceTask` is a long-lived operation over derived state.
- `MaintenanceBudget` is a shared unit budget for a drain.
- `ImmediateScheduler` executes each request synchronously. Self-schedules and
  A-to-B-to-A requests use an allocation-free iterative trampoline, preserving
  one execution per request without recursive task entry. A self-schedule that
  consumes no budget returns `false` to report that the synchronous backend
  cannot make progress. A recursive scheduler lock serializes concurrent
  callers while permitting a running task to schedule itself or another task;
  each external `schedule()` returns only after its own request executes. If a
  callback throws, its active trampoline frame is discarded, including
  reentrant follow-ups accepted into that call-local frame.
- `MaintenanceMetrics` reports schedules, collapsed schedules, executions, and
  capacity failures.

`include/tess/maintenance/scheduler.h` supplies the fixed-registration
contract. It deliberately uses a structural customization boundary:

- `RegisteredScheduler<Backend>` preallocates opaque task slots. Tasks register
  during setup and the registry becomes immutable at `seal()`; post-seal
  release retires a slot permanently rather than reusing its identity.
- `MaintenanceBackend` is a compile-time structural boundary, not a
  virtual ABI. A custom backend supplies construction from capacity, an
  explicit `ScheduleResult`, a `BackendDrainResult` for run/flush, metrics, and
  a no-throw pending query. The facade serializes drain calls, but schedules
  may arrive concurrently with each other and with a drain. A custom backend
  must linearize scheduling against pending observations and drains, avoid
  concurrent invocation of one task, and allow a callback to schedule through
  the same facade. On normal return, `Accepted` linearizes when the offer
  executes synchronously or is retained/coalesced without loss.
  `CapacityExhausted` retains and executes none of that offer, so the caller
  may retry. `has_pending()`
  linearizes against those operations and reports work still retained at its
  observation; a concurrent drain may consume accepted work before its
  `schedule()` call returns. Metrics are thread-safe and monotonic but their
  no-throw diagnostic snapshot need not be transactional across fields while
  operations are in flight.
  If a callback throws, that invocation is consumed rather than restored and
  its execution is counted. Offers coalesced into the same synchronous
  call-local invocation are consumed with it; every independently retained
  accepted offer remains reachable. This is observable with the immediate
  backend, whose reentrant follow-ups belong to its active call frame. The
  exception itself propagates verbatim; authoritative dirty-mask and
  content-version state remains responsible for deciding whether to retry.
  `FixedRegistrationBackend` optionally adds `register_task()` and `seal()` as
  a required pair for implementations such as dirty-bit. At facade sealing,
  each live slot is registered once in slot order before one backend seal. Both
  hooks are no-throw; a rejected registration is an unrecoverable capacity
  mismatch, so partially published setup cannot escape through an exception.
  The facade owns fixed registration when those hooks are absent.
- `MaintenanceHandle` contains private owner-epoch, slot, and generation state.
  Handles are copyable value tokens, but registered tasks and schedulers are
  non-copyable and non-movable. A task must outlive its registration. Destroying
  the scheduler releases every task; destroying a still-registered task fails
  fast in every build.
- `try_schedule()` returns no value for expected stale or foreign-handle
  uncertainty without scheduling work or changing scheduler metrics.
  `schedule()` fails fast for the same misuse. Registration after sealing,
  cross-scheduler task ownership, repeated unchecked release, and lifecycle
  mutation from a callback also fail fast independently of assertions. A
  callback may schedule another task through the same registered scheduler;
  nested identity, scheduling, drain, or lifecycle operations on a different
  registered scheduler fail fast across all backend types, avoiding an
  implicit cross-owner lock order. Calling a drain reentrantly from a callback
  also fails fast. The no-throw, read-only `metrics()` snapshot is deliberately
  callable from any callback owner.
- `ScheduleResult` distinguishes accepted work, retryable queue-capacity
  exhaustion, and an immediate zero-progress stall. `DrainResult`
  distinguishes a positive idle observation, work drained to quiescence,
  budget exhaustion with reachable work, and a zero-progress stall.
  Callback exceptions continue to propagate verbatim; the contract does not
  replace their type and message with a status. The throwing invocation and
  synchronous call-local follow-ups are consumed, independently retained
  accepted work remains reachable, and authoritative dirty-mask and
  content-version state remains responsible for the caller's retry decision.
- `try_release()` returns a `ReleaseResult` that distinguishes release, invalid
  identity, and missing idle evidence. Release never cancels work. After
  sealing, a separate drain must return `Idle` after the last accepted schedule
  before release is allowed; a `Drained` result requires one more observation.
  An in-flight schedule prevents that drain from returning `Idle`. Operations
  use a lifecycle barrier so release cannot race scheduling or draining through
  the wrapper.

`Idle` means only that the scheduler observed no reachable execution at entry
or exit without an intervening accepted schedule. It does not prove external
dirty state is clean, exclude a producer that has not scheduled its dirty
signal, or make world/residency mutation safe. An adapter must separately own
the producer and residency transition around its quiescent boundary: close and
join producers before the drain that establishes `Idle`, then keep them closed
through the residency transition. Likewise, canonical archive equality is
useful authoritative-state evidence but cannot validate dirty masks, content
or topology versions, residency generations, or derived products, which
archives intentionally omit.

## Stable External Chunk Adapter

`include/tess/maintenance/chunk_adapter.h` is the stable world-backed consumer
of the registered contract. `ChunkMaintenanceAdapter` remains external to
storage: it borrows an immovable `World`, owns its scheduler, tasks, handles,
and derived product slots, and never changes world construction or `ChunkMeta`
layout. Its default backend is stable synchronous `ImmediateScheduler`. The
caller gives one nonzero dirty mask to one clearing owner and a typed rebuild
callback. Zero or foreign marks are rejected before mutation.

The operation results are deliberately explicit. `ChunkMarkResult` reports
accepted and rejected marks; `ChunkProductView` combines a product pointer,
`ChunkProductState`, and its token; `ChunkResidencyResult` carries a
`ChunkResidencyStatus`, which also reports reconciliation; `ChunkEvictionResult`
reports an eviction attempt; and `ChunkAdapterReleaseResult` distinguishes
released, not-idle, and already-released adapters.

Concurrent producers may offer scheduler work, subject to the world's
external-synchronization rules. Residency changes and task release instead
require exclusive adapter access: close and join producers before the drain
that establishes `Idle`, and keep them closed through the operation.

Dense task slots map directly to chunk keys. Sparse task slots map to the
world's fixed resident slots and carry `{key, residency_generation}` bindings.
Sparse binding changes are permitted only after producers are closed and
joined and an explicit adapter drain has returned a fresh positive `Idle`.
The caller keeps producers closed through an adapter-owned residency batch.
Direct sparse residency mutation after binding is unsupported; a coordinated
archive load is reconciled explicitly at the same quiescent boundary. Tasks
recheck `resident_ref()` before unchecked sparse access, but those checks do
not make unsynchronized world mutation safe.

Each completed product carries `ChunkProductToken{key, content_version,
residency_generation}` and is classified as unavailable, stale, or current.
The callback writes derived state only; authoritative fields and archive bytes
do not depend on backend selection. After a successful callback the adapter
publishes the token and uses `clear_dirty_observed()` to clear exactly its
observed bits. An intervening mark or residency change leaves the token stale
and work retryable. Because the product token records the shared chunk content
version, a disjoint dirty owner can also stale it without setting this
adapter's bits. An explicit `retry()` rebuilds that version drift even when the
owned observation is empty and never clears the other owner's mask bits. A
callback exception propagates verbatim, may leave a partial but stale product,
leaves the owned dirty signal or shared content-version drift intact, and
requires an explicit caller retry.

`mark_dirty()` records authoritative dirty-mask/content-version state before
scheduling,
so queue capacity failure cannot erase the retry signal. Scheduling remains
coalescing. If a generation-safe clear fails and its follow-up cannot enter a
bounded comparison queue, a preallocated per-slot bit retains that logical
retry. Unbounded `flush()` reoffers this debt before and after backend work; a
post-drain reoffer or still-retained debt reports `BudgetExhausted`, never
`Idle`, and therefore cannot open residency or release. Budgeted drains remain
explicit: `run_some()` only reports retained debt as `BudgetExhausted` because
a structural backend may execute an accepted offer synchronously outside the
supplied budget. Explicit `retry()` or unbounded `flush()` performs
re-admission. Warmed adapter scheduling and draining allocate only if the user
rebuild callback does. The self-checking
`examples/chunk_maintenance.cc` shows the stable immediate default while
inspecting the dirty mask, content version, product token, and backend metrics.
An installed-package consumer compiles and runs the same workflow, and a
second installed-package consumer proves registration, opaque handles, sparse
residency, budgeted draining through a consumer-defined structural backend,
explicit flush, and checked shutdown against only stable spellings.

Queued backends allocate their pointer ring only during construction. A task
must outlive its scheduler or a completed `flush()`. Destroying a scheduler
with pending tasks drops the non-owning pointers without executing them.
Capacity exhaustion returns false; the authoritative dirty signal must remain
set so the caller can retry. Tasks inspect content versions or dirty masks,
clear only the state they actually rebuilt, and may schedule follow-up work
when budgeted work remains. A queued task that successfully schedules any
follow-up on its executing thread without consuming budget stops that drain
with `false` instead of allowing direct or cross-task rescheduling to spin
forever. The follow-up remains queued for caller intervention. A schedule call
from another thread is an independent producer and is not attributed to the
running task. Concurrent drain calls and immediate schedule calls are
serialized, so a task never executes against itself. A task may call only
`schedule()` on a scheduler while it runs; calling `run_some()` or `flush()`
reentrantly is outside the contract because queued drains hold their
non-recursive serialization lock.

`DirtyBitScheduler` has a distinct setup phase. Registration is idempotent up
to the configured capacity, and registration racing with `seal()` either
completes before publication or is rejected. Scheduling and draining are
rejected before sealing, and post-seal registration is rejected. The registry
stores non-owning pointers, so registered tasks must outlive the scheduler or
a completed `flush()`. Destroying the scheduler drops pending bits without
executing tasks. Allocation-sensitive callers must warm every producer and
drain thread with a successful schedule or task execution before entering the
measured or allocation-prohibited region; `seal()` warms no thread. A thrown
task consumes only its own claimed bit; other claimed or concurrently scheduled
tasks remain pending for a later drain.

A queued backend removes an entry before invoking its task. If the task throws,
the exception propagates and that queue entry is not restored. The task's
authoritative dirty-mask and content-version state must remain set; after
inspecting partial effects, the caller decides whether explicitly scheduling a
retry is safe.

Coalescing is not exact-event delivery. Authoritative gameplay events remain
on exact queues and simulation phases. Explicit flush points define when a
consumer may depend on completed derived state.

## Experimental Backends and Promotion Decision

`include/tess/experimental/maintenance.h` retains the virtual
`MaintenanceScheduler` interface plus the `FifoScheduler`,
`CoalescingScheduler`, and registered `DirtyBitScheduler` backends. The
stable facade does not re-export them. An application
may explicitly supply one of those types as a backend, but doing so does not
make that backend or the virtual interface stable.

The registered dirty-bit implementation passed correctness, determinism,
generation-safe clear, exception, shutdown, allocation, ASan/UBSan, and TSan
gates. In the controlled portable campaign, its M3 result was flat with no
material regression. The Steam Deck result was a material regression against
the immediate guardrail in budgeted, flush, and the 256- and 1,024-task scaling
workloads; the 4,096-task scaling result was inconclusive. The cross-hardware
rule therefore keeps dirty-bit experimental. FIFO and queued coalescing remain
experimental comparison machinery for the same reason.

The stable adapter defaults to the measured immediate implementation. No
measured implementation body, adapter body, MNT-3 campaign configuration,
compiler or benchmark flag, benchmark, or fixture changed during graduation,
so the campaign remains representative. The generic paired-sentinel source map
now identifies the stable alias directory as requiring the dedicated campaign;
that CI metadata was not an MNT-3 measurement input. No scheduler is embedded
in world storage, world construction is unchanged, and exact events and
authoritative simulation paths remain outside maintenance. See the
optimization log and design-decision history for the retained measurements and
authority decision.
