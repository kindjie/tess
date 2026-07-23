# Experimental Maintenance Scheduling

`include/tess/experimental/maintenance.h` contains an opt-in experiment for
derived-state maintenance. It does not alter world construction, authoritative
storage, exact event handling, or simulation command execution.

## Experimental Surface

- `MaintenanceTask` is a long-lived operation over derived state.
- `MaintenanceBudget` is a shared unit budget for a drain.
- `MaintenanceScheduler` is the backend-neutral schedule, run, and explicit
  flush interface.
- `ImmediateScheduler` executes each request synchronously.
- `FifoScheduler` is a bounded, non-deduplicating amplification baseline.
- `CoalescingScheduler` retains at most one pending entry per task.
- `MaintenanceMetrics` reports schedules, collapsed schedules, executions, and
  capacity failures.

Queued backends allocate their pointer ring only during construction. A task
must outlive its scheduler or a completed `flush()`. Destroying a scheduler
with pending tasks drops the non-owning pointers without executing them.
Capacity exhaustion returns false; the authoritative dirty signal must remain
set so the caller can retry. Tasks inspect versions or dirty flags, clear only
the state they actually rebuilt, and may schedule themselves when budgeted
work remains. Concurrent drain calls are serialized, so a task never executes
against itself.

Coalescing is not exact-event delivery. Authoritative gameplay events remain
on exact queues and simulation phases. Explicit flush points define when a
consumer may depend on completed derived state.

## Promotion Decision

The prototype remains experimental. It passes correctness, deterministic
1,000-run flush, concurrency, shutdown, and steady-state allocation tests, and
it collapses 512 dense schedules to one execution. On the initial local
five-repetition run, however, sparse coalescing measured 21,069 ns per 256
distinct tasks versus 517 ns for immediate execution. That fails the required
no-more-than-10% sparse overhead gate, so no scheduler hook is integrated into
world storage. See the optimization log for retry conditions.
