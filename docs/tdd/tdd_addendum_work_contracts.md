# TDD Addendum: Work Contracts Evaluation and Integration

## Status

Proposed addendum. Do not integrate into the core library until the success criteria below are met.

## Context

The tile-map library already separates authoritative tile/world state from derived data such as chunk metadata, region graphs, pathfinding caches, render caches, and debug overlays. Many derived systems do not need one exact task per mutation. They need a durable signal meaning:

```text
this chunk/system has pending maintenance work
```

Work Contracts are a candidate abstraction for this deferred maintenance layer. A contract is a long-lived, repeatable unit of work that may be scheduled many times but executed once until it reschedules or becomes unscheduled. This coalescing behavior is potentially useful for dirty chunks, nav-region invalidation, render-cache rebuilds, and budgeted maintenance.

This addendum defines how to test that fit before adopting the pattern.

## Existing Design Alignment

The current library has chunk metadata but not a scheduler:

```text
ChunkMeta::version
ChunkMeta::topology_version
ChunkMeta::field_dirty_flags
ChunkMeta::active_flags
ChunkMeta::dirty_bounds
dirty_chunks(flags)
active_chunks(flags)
```

Those fields are a useful maintenance signal and should remain the immediate
baseline. They are not currently a thread-safe contract system, a job queue, or
an exact-once event log. This addendum evaluates whether to add scheduler hooks
around that metadata later; it does not require current world storage to own
long-lived async work handles.

## Non-Goals

- Do not replace the authoritative simulation event model.
- Do not use contracts for exact-once gameplay events.
- Do not require the tile-map library to depend directly on a specific Work Contracts implementation.
- Do not require all users to use async/deferred maintenance.
- Do not introduce nondeterministic simulation results.
- Do not make `ChunkMeta` atomic or thread-safe by implication.
- Do not make `dirty_chunks(flags)` promise zero allocations or scheduler
  behavior.

Avoid contract-style coalescing for:

```text
damage events
item transfers
quest triggers
combat hits
economy transactions
lockstep/network commands
save-authoritative mutation logs
```

## Hypothesis

A coalescing work-contract-style scheduler will improve maintenance performance and ergonomics when many fine-grained mutations repeatedly dirty the same chunk, region, or derived-data shard.

The expected win is not faster execution of one rebuild. The expected win is avoiding redundant scheduling, redundant tiny jobs, excessive queue traffic, and scattered rebuild orchestration.

## Candidate Integration Points

### 1. Dirty Chunk Maintenance

A chunk owns or references one maintenance handle. Tile writes mark dirty flags and schedule the handle.

```text
set_field<Tag>(Coord3)
  -> mark chunk field_dirty_flags / topology_version / dirty_bounds
  -> optionally schedule chunk maintenance through an injected scheduler
```

The maintenance routine reads dirty flags, rebuilds the required derived outputs, publishes a new version, then exits or self-schedules if budget was exhausted.

The current public API exposes `field<Tag>(Coord3)` and separate
`mark_dirty(...)` calls. A future convenience write API may combine mutation and
dirty marking, but it must not obscure which authoritative field changed or
which derived predicates require maintenance.

### 2. Navigation and Region Graph Repair

Door, terrain, reservation, and obstruction changes mark affected nav chunks or region graph shards dirty. Maintenance rebuilds walkability spans, adjacency, local connectivity, and versioned path metadata.

Individual path requests may still use a normal request/result queue because each request matters independently.

### 3. Renderer and GPU Preparation

Render chunks can use coalesced dirty work for sprite-batch rebuilds, chunk mesh rebuilds, minimap blocks, visibility buffers, debug overlays, and GPU staging ranges.

Final rendering and frame-graph ordering remain explicit and are not managed by contracts.

### 4. Budgeted AI/System Maintenance

AI buckets, job-provider indexes, reservation indexes, and influence maps may self-schedule while they have more work than the current tick/frame budget allows.

## Required Abstraction

The library must depend on a small scheduler interface, not on a concrete Work Contracts library.

A minimal runtime-polymorphic shape for experiments:

```cpp
struct MaintenanceTask {
    virtual ~MaintenanceTask() = default;
    virtual void run(MaintenanceBudget& budget) = 0;
};

struct MaintenanceScheduler {
    virtual ~MaintenanceScheduler() = default;
    virtual void schedule(MaintenanceTask& task) = 0;
    virtual void run_some(MaintenanceBudget budget) = 0;
    virtual void flush() = 0;
};
```

A later production API may use compile-time polymorphism instead. The required semantics are more important than this exact interface.

The scheduler interface should live outside authoritative storage at first,
preferably in an experimental maintenance namespace. `World<Shape, Schema,
Residency>` should remain usable without any scheduler type or linked backend.

## Required Semantics

The scheduler backend must provide these semantics:

```text
schedule() is coalescing
scheduled work may run once after many schedule() calls
a task must inspect dirty flags and version counters
work may self-schedule when incomplete
a task must not run concurrently with itself
a task may run concurrently with unrelated tasks only if configured safely
flush() completes all currently reachable maintenance work or reports failure
shutdown/release is deterministic and safe
```

Dirty metadata semantics must also be defined separately from scheduler
semantics:

```text
mark_dirty() records authoritative mutation impact for one chunk
clear_dirty() may clear only work that was observed and rebuilt
version counters identify mutation generations, not task executions
dirty bounds describe canonical world coordinates, not halo/cache cells
```

If concurrent scheduling is introduced, dirty metadata updates need explicit
synchronization or phase ownership. The existing non-atomic fields are
appropriate only for single-threaded mutation phases or externally synchronized
access.

## Experimental Backends

Implement at least three scheduler backends behind the same test interface:

1. Immediate backend
   - `schedule()` runs the task synchronously or marks it for same-phase execution.
   - Used as the simplest correctness baseline.

2. FIFO queue backend
   - Schedules one queue entry per request, optionally with deduplication disabled.
   - Used to measure queue amplification and redundant work.

3. Coalescing contract backend
   - Schedules one durable handle per chunk/system/shard.
   - Multiple schedules before execution collapse into one pending run.
   - May use the external Work Contracts library or a minimal internal prototype with equivalent semantics.

Optional fourth backend:

4. Coalescing dirty-bit scan backend
   - Maintains dirty bitsets and scans them directly.
   - Useful baseline for chunk-heavy worlds where simple bitset scanning may outperform a scheduler.

The existing `dirty_chunks(flags)` scan is the conceptual baseline for this
backend, but not its final performance form. It currently returns an owning
`std::vector<ChunkKey>`, which is ergonomic but allocates.

## Correctness Tests

### Coalescing Semantics

Given one task scheduled N times before execution, it must execute once unless it self-schedules.

Success criterion:

```text
N schedule calls before run_some() produce exactly 1 execution
```

### Dirty Flag Preservation

If a task is scheduled, starts running, and additional dirty flags are set during or before completion, no dirty state may be lost.

Success criterion:

```text
all dirty flags set before flush() are observed and cleared or intentionally carried forward
```

Tests must cover partial clears. For example, clearing render maintenance must
not accidentally clear terrain or topology dirtiness recorded in the same
`ChunkMeta`.

### Self-Scheduling

A task that exceeds its budget must preserve progress and schedule itself again.

Success criterion:

```text
a task with K units of work and budget B completes in ceil(K / B) executions
```

### Non-Concurrent Self Execution

A single maintenance task must never run concurrently with itself.

Success criterion:

```text
stress tests observe zero overlapping executions of the same task
```

### Deterministic Flush

For a fixed sequence of authoritative tile mutations and an explicit flush point, derived outputs must be identical across backends and runs.

Success criterion:

```text
same mutation seed + flush -> byte-identical derived-state hashes across 1,000 runs
```

### Shutdown and Release Safety

Destroying a world, chunk, or scheduler while work is pending must not access freed memory.

Success criterion:

```text
ASan/TSan/UBSan clean under pending-work destruction tests
```

## Benchmark Scenarios

Benchmarks must run against all scheduler backends.

### Scenario A: Dense Tile Edits in One Chunk

Repeatedly mutate many tiles in the same chunk.

Measures whether coalescing avoids redundant work.

Parameters:

```text
chunk_size: 16, 32, 64
mutations_per_tick: 1, 8, 64, 512, 4096
dirty_categories: terrain, region, render, nav
```

### Scenario B: Sparse Edits Across Many Chunks

Mutate one or a few tiles across many chunks.

Measures scheduler overhead and scalability.

Parameters:

```text
chunk_count: 256, 4096, 65536
active_dirty_chunks: 1%, 10%, 50%, 100%
```

### Scenario C: Path/Nav Invalidation Storm

Toggle many doors, blockers, or terrain cells while path requests continue reading versioned nav data.

Measures rebuild latency and stale-version behavior.

Parameters:

```text
nav_chunk_size: 16, 32, 64
updates_per_tick: 16, 256, 4096
path_queries_per_tick: 64, 1024, 16384
```

### Scenario D: Budgeted Maintenance

Give each maintenance phase a fixed budget and force tasks to self-schedule.

Measures progress fairness and tail latency.

Parameters:

```text
budget_units_per_tick: 64, 256, 1024, 4096
work_units_per_dirty_chunk: 1, 8, 64, 512
```

### Scenario E: Render Cache Rebuild

Dirty render chunks due to terrain edits, visibility changes, and overlay changes.

Measures whether coalescing reduces redundant rebuilds before the next render publish point.

Parameters:

```text
render_chunks: 256, 4096, 65536
edits_per_frame: 1, 32, 1024, 32768
publish_frequency: every frame, every 2 frames, every 8 frames
```

## Metrics

Collect at minimum:

```text
schedule calls
actual task executions
redundant executions
dirty flags processed
mean / median / p95 / p99 maintenance latency
worst-case flush time
worker CPU time
wall-clock time
atomic operation count if measurable
allocations
cache misses where available
contention stalls where available
ASan/TSan/UBSan results
```

Derived metrics:

```text
coalescing ratio = schedule calls / actual executions
maintenance amplification = actual executions / dirty chunks processed
latency per dirty chunk
flush cost per dirty chunk
```

## Explicit Success Criteria

Work Contracts, or a contract-style scheduler, may be integrated only if all mandatory criteria pass.

### Correctness Criteria

Mandatory:

```text
all correctness tests pass for immediate, FIFO, and contract backends
derived-state hashes are deterministic after explicit flush points
no lost dirty flags under concurrent scheduling stress tests
no self-concurrent task execution
clean ASan, TSan, and UBSan runs
safe shutdown with pending scheduled work
```

### Performance Criteria

Mandatory:

```text
contract backend is not more than 10% slower than immediate backend for single-threaded sparse edits
contract backend reduces task executions by at least 4x versus non-deduplicated FIFO in dense-edit scenarios
contract backend reduces p95 maintenance latency by at least 25% versus non-deduplicated FIFO in at least two dirty-chunk scenarios
contract backend has no worse than 15% higher worst-case flush time than the best tested backend in sparse-edit scenarios
contract backend performs zero dynamic allocations per schedule() call in steady state
```

Conditional acceptance:

```text
If a simple dirty-bit scan backend beats contracts by more than 20% in all chunk scenarios, prefer dirty-bit scanning for chunks and reserve contracts for non-chunk systems.
```

### Ergonomics Criteria

Mandatory:

```text
chunk maintenance code is simpler or no more complex than the FIFO backend
ownership and release rules are documented in the API comments
users can provide their own scheduler backend
contracts are optional and can be compiled out or not linked
the existing immediate/explicit dirty-scan workflow remains available
```

### Determinism Criteria

Mandatory:

```text
authoritative simulation output is independent of maintenance execution order
deferred maintenance only affects derived caches or explicitly versioned outputs
save/load after flush produces identical authoritative state across backends
```

## Integration Plan Upon Success

### Phase 1: Internal Experimental Backend

Keep the scheduler behind a private test interface. Do not expose it in public headers except through experimental namespaces.

```text
namespace tess::experimental::maintenance
```

Experiments may adapt existing `ChunkMeta` and `dirty_chunks(flags)`, but must
not require changes to `World` construction or field access for users that do
not opt in.

### Phase 2: Public Scheduler Hook

Expose a small scheduler concept or interface that games can implement.

Required public behavior:

```text
coalescing schedule handles are supported
immediate maintenance remains available
flush points are explicit
scheduler choice does not change authoritative simulation behavior
```

### Phase 3: Chunk Maintenance Integration

Integrate only for derived chunk maintenance first.

Initial supported derived systems:

```text
chunk dirty flags
region metadata
local nav metadata
render-cache invalidation metadata
```

Do not integrate into path request execution, gameplay event handling, or simulation command processing.

Integration should prefer an external owner or adapter for maintenance handles.
Embedding handles directly in `ChunkMeta` should require a separate decision,
because `ChunkMeta` is currently simple value metadata stored alongside pages.

### Phase 4: Optional Backend Package

If the external Work Contracts library is used, keep it optional:

```text
TESS_ENABLE_WORK_CONTRACTS=ON/OFF
```

The core library must still build without it.

## API Documentation Requirements

Public docs must state:

```text
schedule() is coalescing, not exact-once
maintenance tasks must inspect dirty flags
maintenance tasks may self-schedule
flush points define when derived state must be complete
contracts must not mutate authoritative state outside explicit simulation phases
```

Every example must show dirty flags or version counters. No example should imply that each schedule call maps to one execution.

## Risks

### Semantic Confusion

Users may mistake coalesced scheduling for a job queue.

Mitigation:

```text
avoid naming public APIs enqueue_job or submit_task
use names like mark_dirty, schedule_maintenance, run_maintenance, flush_maintenance
```

### Hidden Nondeterminism

Async maintenance may accidentally affect simulation decisions.

Mitigation:

```text
separate authoritative state from derived caches
require explicit flush before deterministic reads
version all derived outputs
include determinism tests in CI
```

### Scheduler Overhead

Contract-style scheduling may be slower than simple dirty-bit scanning for chunk-heavy workloads.

Mitigation:

```text
benchmark against dirty-bit scan backend
allow backend choice per subsystem
```

### Lifetime Bugs

Long-lived scheduled handles may outlive chunks, worlds, or captured objects.

Mitigation:

```text
RAII release handles
shutdown tests with pending work
no raw this captures in examples unless lifetime is proven
```

## Decision Record Template

When the experiment completes, record:

```text
backend versions tested
platforms tested
benchmark results
correctness results
sanitizer results
chosen integration scope
rejected integration points
remaining risks
```

## Recommendation

Proceed with an experiment, not direct adoption.

The likely successful integration is a backend-agnostic deferred maintenance system inspired by Work Contracts. The public tile-map library should expose coalesced maintenance semantics and scheduler hooks, while keeping authoritative simulation events on deterministic queues or explicit phases.
