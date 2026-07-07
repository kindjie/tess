# Diagnostics Foundation

The diagnostics layer provides opt-in, compile-time-gated counters for path
search, allocation tracking, and queued phase execution. It lives in
`include/tess/diagnostics/diagnostics.h` and is exported by `tess/tess.h`.

## Public Surface

- `TESS_ENABLE_DIAGNOSTICS` is the compile-time gate. When it is defined,
  `TESS_DIAGNOSTICS_ENABLED` is `1` and the counter types below exist; when
  it is not defined, `TESS_DIAGNOSTICS_ENABLED` is `0`, every diagnostic
  macro expands to an empty statement, and the counter types are not
  declared at all.
- Event macros keep instrumentation out of release builds:
  `TESS_DIAGNOSTIC_ONLY(expr)` runs an expression only when enabled,
  `TESS_DIAGNOSTIC_INC(counter)` and `TESS_DIAGNOSTIC_ADD(counter, value)`
  bump caller-visible counters, and `TESS_DIAG_EVENT(name)` /
  `TESS_DIAG_EVENT_VALUE(name, value)` call the matching
  `tess::diagnostics::event_<name>` hook.
- `PathCounters` records path-search internals: scratch clears
  (`event_path_clear`), initializations (`event_path_initialize`),
  start/goal passability checks (`event_path_start_passability_check`,
  `event_path_goal_passability_check`), heap pushes and pops
  (`event_path_heap_push`, `event_path_heap_pop`), stale and closed pops
  (`event_path_skip_pop`), neighbor candidates, passability checks, cost
  reads, blocked and closed neighbors (`event_path_neighbor_candidate`,
  `event_path_passability_check`, `event_path_cost_read`,
  `event_path_neighbor_blocked`, `event_path_neighbor_closed`), relax
  attempts and successes (`event_path_relax_attempt`,
  `event_path_relax_success`), touched nodes (`event_path_touch_node`),
  heuristic calls (`event_path_heuristic`), and reconstructed nodes
  (`event_path_reconstruct_node`).
- `AllocationCounters` records allocation and deallocation counts and bytes
  through `record_allocation(size)` and `record_deallocation(size)`.
- `QueuedPhaseCounters` records queued phase execution: validated phase
  calls and operations (`event_queued_phase_execute`), invalid phase ranges
  (`event_queued_phase_invalid_range`), phase failures
  (`event_queued_phase_failure`), partitioned phase calls and dirty
  partitions (`event_queued_partitioned_phase`), scoped-thread dispatches
  and worker counts (`event_queued_scoped_thread_dispatch`), worker-pool
  dispatches and worker counts (`event_queued_worker_pool_dispatch`), and
  collected and merged dirty records (`event_queued_dirty_collect`,
  `event_queued_dirty_merge`).
- `ScopedPathCounters`, `ScopedAllocationCounters`, and
  `ScopedQueuedPhaseCounters` are RAII scopes that install a caller-owned
  counter struct as the active sink for the current thread and restore the
  previous sink on destruction. They are non-copyable, and each counter
  struct has a `reset()` helper.

## Behavior

Event hooks are no-ops unless a matching scoped counter object is active on
the calling thread; installing a scope is the only way to start recording.
Scopes nest: the innermost active scope receives events, and destroying it
restores the outer scope.

The active counter sinks are `thread_local` pointers. This is a deliberate
scope limitation: a counter scope installed on one thread observes only
events raised by that thread, so counters do not aggregate across worker
threads. `ScopedThreadPhaseExecutor` in the queued layer, for example,
records its dispatch counts on the caller thread before launching workers,
and worker callbacks do not mutate the caller's queued-phase counters. A
future worker-pool backend needs per-worker sinks plus explicit reduction
before cross-thread totals can be trusted.

Because the counter types only exist when `TESS_ENABLE_DIAGNOSTICS` is
defined, code that declares counter objects or scopes must itself be
guarded (tests use dedicated diagnostics-enabled targets). Instrumentation
sites in library code use only the macros, which compile away cleanly in
non-diagnostic builds.

## Deliberate Limits

This slice is counters only. It does not implement structured event logs,
timing capture, sampling profiler hooks, cross-thread aggregation, or any
runtime toggle; enabling or disabling diagnostics is a recompile.
