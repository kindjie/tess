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
  each external `schedule()` returns only after its own request executes.
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
