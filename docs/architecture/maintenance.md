# Experimental Maintenance Scheduling

`include/tess/experimental/maintenance.h` contains an opt-in experiment for
derived-state maintenance. It does not alter world construction, authoritative
storage, exact event handling, or simulation command execution. Consumers opt
in with `#include <tess/experimental/maintenance.h>`; the stable compatibility
umbrella `tess/tess.h` deliberately does not include experimental headers. No
world or scheduler adopts the experiment implicitly.

## Experimental Surface

- `MaintenanceTask` is a long-lived operation over derived state.
- `MaintenanceBudget` is a shared unit budget for a drain.
- `MaintenanceScheduler` is the backend-neutral schedule, run, and explicit
  flush interface.
- `ImmediateScheduler` executes each request synchronously. Self-schedules and
  A-to-B-to-A requests use an allocation-free iterative trampoline, preserving
  one execution per request without recursive task entry. A self-schedule that
  consumes no budget returns `false` to report that the synchronous backend
  cannot make progress. A recursive scheduler lock serializes concurrent
  callers while permitting a running task to schedule itself or another task;
  each external `schedule()` returns only after its own request executes. If a
  callback throws, its active trampoline frame is discarded, including
  reentrant follow-ups accepted into that call-local frame.
- `FifoScheduler` is a bounded, non-deduplicating amplification baseline.
- `CoalescingScheduler` retains at most one pending entry per task. Its
  preallocated membership index makes admission independent of queue depth.
- `DirtyBitScheduler` is the selected chunk-maintenance candidate. An external
  owner registers long-lived tasks during setup, calls `seal()`, and then may
  schedule registered tasks concurrently without a producer lock. After a
  thread's first successful post-seal schedule or first task execution,
  scheduling is allocation-free on that thread; either first use may initialize
  platform thread-local runtime state. Atomic task bits coalesce repeated
  schedules; drains are serialized and visit tasks in registration order.
- `MaintenanceMetrics` reports schedules, collapsed schedules, executions, and
  capacity failures.

`include/tess/experimental/registered_maintenance.h` adds the fixed-registration
contract candidate used by the next adapter experiment. It deliberately wraps,
rather than promotes, the existing backends:

- `RegisteredScheduler<Backend>` preallocates opaque task slots. Tasks register
  during setup and the registry becomes immutable at `seal()`; post-seal
  release retires a slot permanently rather than reusing its identity.
- `MaintenanceBackend` is a compile-time structural boundary, not a stable
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
  exception itself propagates verbatim; authoritative dirty/version state
  remains responsible for deciding whether to retry.
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
  accepted work remains reachable, and authoritative dirty/version state
  remains responsible for the caller's retry decision.
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
useful authoritative-state evidence but cannot validate dirty flags, versions,
residency generations, or derived products, which archives intentionally omit.

Queued backends allocate their pointer ring only during construction. A task
must outlive its scheduler or a completed `flush()`. Destroying a scheduler
with pending tasks drops the non-owning pointers without executing them.
Capacity exhaustion returns false; the authoritative dirty signal must remain
set so the caller can retry. Tasks inspect versions or dirty flags, clear only
the state they actually rebuilt, and may schedule follow-up work when budgeted
work remains. A queued task that successfully schedules any synchronous
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
authoritative dirty/version state must remain set; after inspecting partial
effects, the caller decides whether explicitly scheduling a retry is safe.

Coalescing is not exact-event delivery. Authoritative gameplay events remain
on exact queues and simulation phases. Explicit flush points define when a
consumer may depend on completed derived state.

## Promotion Decision

The registered dirty-bit backend passed the experimental promotion criteria
and is the preferred backend for a future external chunk-maintenance adapter.
It passes deterministic 1,000-run flush, canonical archive equivalence,
concurrency, generation-safe dirty clear, budget, exception, shutdown, and
steady-state allocation contracts; the maintenance suite is also clean under
ASan/UBSan and TSan. It collapses 512 dense schedules to one execution.

In the local evaluation, dirty-bit scheduling was faster than immediate
execution for the sparse synthetic case, reduced p95 latency against FIFO by
more than 75% in sparse, dense, and mixed dirty-chunk workloads, and had the
lowest deferred sparse flush time. It also beat queued coalescing by more than
20% in all three chunk workloads, selecting the dirty-bit fallback under the
TDD's conditional rule. The new scenario thresholds remain informational
until representative Linux main-tier calibration exists.

The API remains experimental and opt-in. No scheduler is embedded in world
storage, world construction is unchanged, and exact event and authoritative
simulation paths remain outside maintenance. The next integration step is an
external owner or adapter that binds registered tasks to derived chunk state.
See the optimization log and design-decision history for the measurements and
boundary.
