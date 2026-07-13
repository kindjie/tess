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
  calls and operations (`event_queued_phase_execute`), invalid phase tokens
  (`event_queued_phase_invalid_range`), phase failures
  (`event_queued_phase_failure`), partitioned phase calls and dirty
  partitions (`event_queued_partitioned_phase`), scoped-thread dispatches
  and worker counts (`event_queued_scoped_thread_dispatch`), worker-pool
  dispatches and worker counts (`event_queued_worker_pool_dispatch`), and
  collected dirty records and merged dirty chunks
  (`event_queued_dirty_collect`, `event_queued_dirty_merge`). Exceptional
  coalescing reports both quantities separately, so duplicate records count
  toward collection while each affected chunk counts once toward merge.
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

## Warning Sink

`include/tess/diagnostics/warning_sink.h` adds an opt-in channel for
structured warnings, gated by the same `TESS_ENABLE_DIAGNOSTICS` switch (the
types do not exist when it is undefined).

- `Warning` is a non-owning record: a `WarningCategory` origin tag, a
  `std::string_view message`, a numeric `detail`, and a `std::source_location
  where` that defaults to the construction site. As with `PathView`, the
  `message` must reference storage that outlives every sink that retains the
  warning (string literals or other static storage); a sink copies the record
  by value but never the pointed-to characters. This precondition is not
  compiler-enforceable.
- `WarningSink` is a concept: any type with a `noexcept warn(const Warning&)`.
  `NullWarningSink` discards every warning and is the zero-cost default for a
  parameter that must satisfy the concept.
- `BufferedWarningSink<Capacity>` is a caller-owned fixed-capacity ring with
  inline `std::array` storage, so `warn()` never allocates. When full it
  overwrites the oldest warning and counts the loss in `dropped()`; indexing
  is oldest-first (`operator[](0)` is the oldest retained warning). `clear()`
  resets the window and the dropped count.

No tess library code raises warnings yet; the sink is a foundational primitive
for later stages (queued-ops result reasons, scheduler budgets).

## Trace Buffer and Timers

`include/tess/diagnostics/trace.h` adds a structured event log and timing
capture, gated by the same `TESS_ENABLE_DIAGNOSTICS` switch.

- `TraceCategory` is a coarse origin tag (`General`, `Path`, `Topology`,
  `Queued`, `Planner`, `Scheduler`, `Render`); `Count` is a sentinel used to
  size the per-category timing array and must not be recorded against.
  `trace_category_count` is the corresponding public array-bound constant.
- `TraceRecord` is one structured point: a category, a non-owning
  `std::string_view label` (same static-storage contract as `Warning::message`
  and `PathView`), a `value` datum, and a monotonic `sequence` ordinal.
- `TraceBuffer` is caller-owned. It wraps a `std::span<TraceRecord>` the caller
  supplies (which must outlive the buffer and any scope targeting it) and holds
  an inline per-category `TraceCategoryStats` accumulator, so nothing here
  allocates. `record()` appends to the ring (overwriting oldest, counting
  `dropped()`, keeping sequence gaps visible); an empty span is valid and drops
  every record. `record_timing()` folds a nanosecond sample into a category's
  accumulator (`samples`, `total_ns`, `min_ns`, `max_ns`; the first sample sets
  both min and max; out-of-range categories are ignored). `total_ns` wraps only
  after ~584 years of accumulated time and is treated as unbounded.
- `ScopedTrace` installs a `TraceBuffer` as the thread's active buffer with the
  same nestable, non-copyable RAII pattern as the counter scopes; `trace_event`
  and the `TESS_DIAG_TRACE` / `TESS_DIAG_TRACE_VALUE` macros route to it (and
  compile to nothing when diagnostics are off). Worker threads do not feed the
  installer's buffer -- the same deliberate `thread_local` limit as the
  counters.
- `ScopedTimer` is a wall-clock (`steady_clock`) RAII timer. It binds to the
  buffer active **at construction**, so a timer started outside any
  `ScopedTrace` records nothing even if a buffer is installed before it ends,
  and nested scopes attribute timing to the buffer that was active when the span
  began. On destruction it folds the elapsed nanoseconds into the category's
  timing accumulator and appends a record whose `value` is that duration.

## Planner Trace

The queued-ops planner (`ops/queued.h`) records its per-operation decisions to
the active trace buffer under the `Planner` category, using the
`TESS_DIAG_TRACE_VALUE` macro so the instrumentation compiles away when
diagnostics are off. Each record's `value` is the operation (or phase) index:

- `plan_operations` emits `invalid_identity`, `invalid_write_policy`,
  `invalid_field_access`, `invalid_domain`, `conflict` (a field hazard against
  an earlier operation), or `planned` (accepted) for each operation.
- `plan_parallel_execution_phases` emits `unsupported_write_policy`,
  `new_phase` (an operation that opens a new parallel phase, whether the first
  or one forced by a conflict), or `merged` (an operation folded into the
  current phase).

This is the first library code to feed the trace buffer; a consumer installs a
`ScopedTrace` around a plan call to capture the decision log.

## Snapshot Export

`include/tess/diagnostics/export.h` provides plain, self-contained snapshot
structs so a panel or consumer can hold diagnostics without touching the live
sinks. `TimingSnapshot` copies every category's `TraceCategoryStats` out of a
`TraceBuffer` (with a Count-guarding `category()` accessor); `DiagnosticsSnapshot`
bundles copies of the `PathCounters`, `AllocationCounters`, and
`QueuedPhaseCounters` a caller owns alongside a `TimingSnapshot`. `capture_timing`
and `capture_diagnostics` assemble them as pure copies, so a snapshot outlives
its sources unchanged.

## ImGui Panels (opt-in)

`include/tess/debug/imgui/panels.h` provides reference Dear ImGui panels over
the export snapshots. It is doubly gated -- the body exists only when the
consumer defines both `TESS_ENABLE_IMGUI` and `TESS_ENABLE_DIAGNOSTICS` on its
own target -- and tess core never fetches or links Dear ImGui. `tess.h` does
not include it.

- The consumer must include `<imgui.h>` **before** `panels.h`; the header
  emits a `#error` if `IMGUI_VERSION` is undefined when both gates are on, so a
  misordered include fails loudly instead of with name-lookup errors.
- The panels use only the three most stable ImGui text primitives (`Text`,
  `TextUnformatted`, `Separator`) and print `uint64` values through
  `unsigned long long` casts for portable printf-style formatting, so they
  compile across ImGui versions.
- `draw_timing_panel(TimingSnapshot)` renders the per-category timing table;
  `draw_path_counters_panel`, `draw_queued_counters_panel`, and
  `draw_allocation_counters_panel` render their counter structs; and
  `draw_diagnostics_panel(DiagnosticsSnapshot)` draws every section in order.
  `category_name(TraceCategory)` maps a category to a label for custom panels.

tess validates the header in CI against a minimal ImGui stub
(`tests/imgui_stub/imgui.h`, `tess_diagnostics_panels_test`); the real Dear
ImGui build is exercised by a downstream consumer.

## Deliberate Limits

Beyond the counters, warning sink, trace/timing, planner trace, snapshot
export, and the opt-in ImGui panels above, this layer does not yet implement a
sampling profiler, cross-thread aggregation, or any runtime toggle; enabling or
disabling diagnostics is a recompile.
