# TDD Addendum: Concurrent Tile-World Execution and Maintenance

## Status

Proposed addendum. This document is design intent for future work. It does
not introduce an executor dependency, a production worker pool, or asynchronous
maintenance behavior by itself.

## Context

Queued operations now have deterministic planning, conservative phase grouping,
deferred dirty accumulation, partitioned dirty scratch, serial phase execution,
and a scoped-thread executor prototype. That work proves a narrow handoff for
parallel execution of already planned work.

Maintenance scheduling is a different problem. Dirty chunks, navigation shards,
render caches, and budgeted derived-state rebuilds often need coalescing:
repeated schedules for the same dirty target may collapse into one later
execution. That is useful for maintenance, but it is not exact-once event
delivery and must not be used for authoritative gameplay events.

This addendum keeps those lanes separate:

- Scoped phase execution: deterministic parallel execution of already planned
  operations over disjoint chunk domains, using Tess-owned phase barriers,
  dirty partitions, and ordered reduction.
- Coalesced maintenance scheduling: deferred derived-state work for dirty
  chunks, nav shards, render caches, and budgeted maintenance, where repeated
  schedules may collapse into one execution.

The existing
[Work Contracts addendum](tdd_addendum_work_contracts.md)
remains the focused evaluation plan for one candidate maintenance scheduler
style. This document defines the broader tile-world concurrency contract that
any backend must satisfy.

## Key Implementation Principles

### Determinism First

Authoritative simulation output must match serial replay for the same inputs,
seed, and explicit synchronization points. Parallelism may change wall-clock
timing and diagnostics, but not authoritative world hashes, operation order,
save data, command outcomes, combat, economy, or path decisions that depend on
authoritative state.

### Ownership Beats Atomics

Tile and chunk mutation is parallel only when the planner proves disjoint
ownership. Tess should prefer phase ownership and non-overlapping mutable
domains over atomic world fields. Atomics may be appropriate for scheduler
internals, but they are not a substitute for explicit world mutation ownership.

### Dirty Metadata Is Reduced After Work

Worker callbacks write caller-owned dirty partitions. They do not mutate shared
`ChunkMeta` fields directly. After workers complete or join, the caller reduces
operation results in plan order and merges dirty records on the main thread or
another explicitly owned reduction context.

### Coalescing Is Not Exact-Once

Maintenance handles represent readiness to rebuild derived state, not event
identity. A coalesced scheduler may turn many schedules into one execution.
It is invalid for gameplay events, save logs, commands, combat, economy, or
other authoritative events where every occurrence matters.

### Backend Abstraction First

Tess owns the executor and scheduler interfaces before adopting external
libraries. Candidate libraries may be tested behind those interfaces, but they
must not become public storage types, required dependencies, or implicit
semantic owners.

### Warm Hot Paths Are Allocation-Free

Phase dispatch, partitioned dirty recording, dirty reduction, and maintenance
`schedule()` must support explicit reserve/setup paths. After setup, warmed
hot paths must perform zero dynamic allocations unless a documented capacity
limit is exceeded and the API reports that growth.

### Shutdown and Flush Are API Semantics

Worker stop, maintenance release, and `flush()` behavior are part of the API,
not caller folklore. A backend must define what work is reachable, what
completion means, how blocked workers wake, and how failures are reported.

### Diagnostics Are Observational

Diagnostics must explain phase grouping, dispatch counts, dirty merge counts,
worker counts, queue amplification, coalescing ratios, and failures without
affecting deterministic state or authoritative ordering.

## Invariants

- `ChunkMeta` remains non-atomic unless a future design explicitly changes it.
- `ReadOnly` operations may overlap chunk domains and receive const access.
- `UniquePerChunk` mutable operations may run together only when phase
  planning proves disjoint chunk domains.
- `UniquePerTile` remains out of scope until Tess owns tile subdomain
  validation and tile-level ownership.
- Parallel phase callbacks must complete, join, or otherwise make all callback
  writes visible before the phase executor returns.
- Operation results are reduced in plan order, independent of worker completion
  order.
- Dirty records are sorted and coalesced by chunk key before mutating
  metadata.
- Scheduler backends cannot change authoritative simulation order or output.
- Maintenance `flush()` must either complete all reachable work or report a
  deterministic failure.
- User callbacks remain responsible for synchronizing mutable state they
  capture outside Tess-owned world partitions.

## Lane 1: Scoped Phase Execution

Scoped phase execution runs a planner-issued `ExecutionPhase` capability
against the exact `ExecutionPlan` that issued it. Planning has already
validated the operation domains and grouped only compatible operations into a
phase; phase helpers reject capabilities copied from another plan or retained
after that plan's owning report is reused.

The first production shape remains conservative:

```text
plan_operations()
plan_parallel_execution_phases()
prepare phase scratch
execute phase through a Tess executor interface
reduce operation results in plan order
merge dirty partitions deterministically
```

The serial executor is the correctness baseline. The scoped-thread prototype
is useful for proving ownership, visibility, and result reduction, but it
creates threads per phase and is not the target production backend.

A future persistent worker-pool backend must:

- avoid per-phase thread creation;
- accept Tess-owned operation-index ranges;
- never dispatch hidden nested parallel work in v1;
- write dirty records into per-operation or per-worker partitions;
- join or complete all phase callbacks before returning;
- expose diagnostics without worker callbacks mutating shared diagnostic
  state; and
- preserve serial world hashes across randomized legal phase plans.

## Lane 2: Coalesced Maintenance Scheduling

Coalesced maintenance scheduling is for derived-state work that can be delayed
until an explicit maintenance point or budgeted background pass. Candidate
targets include:

- chunk dirty flag maintenance;
- navigation chunk and region graph shard rebuilds;
- render chunk cache rebuilds;
- minimap, visibility, and debug overlay invalidation;
- AI indexes, reservation indexes, and influence maps; and
- budgeted derived-state passes that may self-schedule when incomplete.

The scheduler owns readiness and lifecycle. Dirty metadata owns the facts that
make the work necessary. A task must inspect dirty flags, versions, and bounds
when it runs; it must not assume each `schedule()` call corresponds to one
execution.

Maintenance is forbidden for:

```text
damage events
item transfers
quest triggers
combat hits
economy transactions
lockstep/network commands
save-authoritative mutation logs
```

The first scheduler API should live behind a Tess-owned experimental
interface. `World<Shape, Schema, Residency>` must remain usable without a
scheduler type, worker pool, or external concurrency dependency.

## Dirty Metadata Protocol

Concurrent or budgeted maintenance needs a generation-aware observe/clear
protocol so rebuilds do not lose dirty flags set during execution.

Required behavior:

- `mark_dirty()` records authoritative mutation impact for one chunk.
- A maintenance pass observes dirty flags, dirty bounds, and generation values
  before rebuilding.
- A clear may remove only the flags and bounds covered by the observed
  generation.
- Dirty flags set after observation are preserved or intentionally carried
  forward.
- Partial clears must not erase unrelated dirty categories in the same
  `ChunkMeta`.
- Dirty bounds remain canonical world-coordinate bounds, not halo or cache
  implementation cells.

This protocol is separate from scheduler semantics. A scheduler may coalesce
work readiness, but dirty metadata decides whether derived state is current.

## External Repository Findings

The 2026-06-08 research snapshot used these public pins:

- `buildingcpp/work_contract`
  at `3f56a17e36db57846a086e20d8788478287f3c86`
  (commit date 2025-11-16)
- `buildingcpp/signal_tree`
  at `f7b59510e117bc6156af86a6b8689ca4a3832e3c`
  (commit date 2026-05-23)

Public sources:

- https://github.com/buildingcpp/work_contract
- https://github.com/buildingcpp/signal_tree
- https://www.buildingcpp.com/documents/work_contract.pdf
- https://www.youtube.com/watch?v=oj-_vpZNMVw
- https://www.youtube.com/watch?v=5ghAa7B5bF0

`signal_tree` is an idempotent readiness-selection primitive. It stores signal
ids, not work payloads. It can help select ready work when FIFO order is not
required, but it does not provide phase completion, result reduction, worker
lifetime, dirty-merge semantics, or authoritative ordering.

`work_contract` is closer to maintenance scheduling than scoped phase
execution. The pinned README and design notes describe recurrent contracts,
coalesced scheduling, blocking and non-blocking groups, async release, and
exception callbacks. Those lifecycle semantics are stronger than Tess needs
for scoped phase execution, where work is already planned, bounded by a phase
barrier, reduced in plan order, and owned by the caller.

First use, if any, should be a backend experiment behind Tess-owned
interfaces, not a public dependency or direct storage integration.

## Failure Modes and Mitigations

### Data Races on World Fields or `ChunkMeta`

Mitigation:

- use phase ownership rules for mutable work;
- keep worker dirty writes in caller-owned partitions;
- keep `ChunkMeta` reduction single-owner; and
- run ASan and TSan stress suites over repeated create/run/flush/stop cycles.

### Lost Dirty Flags During Maintenance

Mitigation:

- use generation-based observe/clear;
- test partial clears for every dirty category; and
- preserve flags set during a rebuild unless the maintenance pass explicitly
  observes and rebuilds that generation.

### Nondeterministic Results

Mitigation:

- keep stable planning order;
- reduce operation results in plan order;
- sort/coalesce dirty records by chunk key; and
- compare serial and parallel world hashes across randomized legal phase plans.

### Oversubscription or Nested Parallelism

Mitigation:

- define an explicit worker-count policy;
- do not hide nested dispatch in v1; and
- record worker count, backend, and phase count in every benchmark result.

### Deadlock or Unsafe Shutdown

Mitigation:

- use RAII pool stop;
- make maintenance `flush()` and shutdown semantics explicit; and
- test blocked worker wakeup and pending-work destruction.

### Queue Amplification

Mitigation:

- compare FIFO, dirty-bit scan, and coalesced scheduler backends; and
- report schedule calls, actual executions, redundant executions, and
  coalescing ratio.

### Misusing Coalescing for Gameplay

Mitigation:

- state non-goals in TDDs and API docs;
- avoid names such as `enqueue_job` for coalesced maintenance; and
- keep authoritative events on deterministic queues or explicit phases.

### Dependency Lock-In

Mitigation:

- keep Tess-owned backend interfaces;
- record dependency docs before any adoption; and
- keep external libraries optional until benchmarks and tests justify them.

### Hidden Allocations

Mitigation:

- provide reserve APIs;
- test warmed phase dispatch and `schedule()` allocation counts; and
- report capacity growth explicitly.

### Performance Regressions Hidden by Timing Noise

Mitigation:

- add benchmark thresholds only after baseline data;
- record backend, worker count, phase count, chunk count, seed, diagnostics
  level, compiler, build mode, CPU, and OS; and
- keep thread-count/backend/seed in benchmark output.

## Performance Criteria

Maintenance scheduler criteria carried forward from the Work Contracts
addendum:

- Contract backend no more than 10% slower than immediate for sparse
  single-thread scheduling.
- At least 4x fewer task executions than non-deduplicated FIFO for dense
  repeated dirty scheduling.
- At least 25% lower p95 maintenance latency than FIFO in two dirty-chunk
  scenarios.
- Worst-case `flush()` no more than 15% slower than the best backend in
  sparse scenarios.
- Zero dynamic allocations per steady-state `schedule()` after setup.
- Prefer dirty-bit scan if it beats contracts by more than 20% in all chunk
  scenarios.

Additional scoped phase-execution criteria:

- Serial executor remains the correctness baseline.
- Persistent worker-pool prototype must avoid per-phase thread creation.
- Warm partitioned phase execution and dirty merge must allocate zero times
  after reserve.
- Parallel execution must match serial world hashes across randomized legal
  phase plans.
- Benchmarks must record backend, worker count, phase count, chunk count,
  seed, diagnostics level, compiler, build mode, CPU, and OS.
- Threshold JSON should be added only after baseline runs, following the
  existing benchmark-plan threshold process.

## Future Test Plan

Tests must be written before implementation in the relevant slice:

- Serial vs scoped-thread vs future pool replay produces identical world
  hashes.
- Mutable overlapping chunks are separated or rejected; disjoint chunks run in
  one phase.
- Read-only overlapping chunks can run together with const access.
- Dirty partitions merge deterministically by chunk key and preserve dirty
  masks and bounds.
- Maintenance schedules coalesce repeated dirty marks into one execution
  unless work self-schedules.
- Dirty flags set during maintenance are preserved or intentionally carried
  forward by generation.
- `flush()` completes reachable work and shutdown wakes blocked workers.
- Warm dispatch and `schedule()` paths do not allocate after reserve.
- ASan and TSan stress tests cover repeated create/run/flush/stop cycles.

## Unknowns and Resolution Steps

- Persistent pool shape: prototype a Tess-owned pool behind the phase executor
  interface, then compare against the scoped-thread executor and serial
  baseline.
- Dirty generation layout: design generation counters and partial-clear tests
  before allowing concurrent maintenance to mutate metadata.
- Maintenance API names: review names in API docs before implementation to
  avoid implying exact-once queue semantics.
- Worker-count policy: define default, minimum, maximum, and nested-dispatch
  behavior before production pool benchmarks.
- Baseline thresholds: collect benchmark data first, then add threshold JSON
  following `docs/planning/benchmark-plan.md`.
- External backend fit: test `work_contract` and `signal_tree` only behind
  Tess-owned interfaces after internal backends establish correctness and
  baseline performance.

## Acceptance for This Documentation Slice

- This TDD is decision-complete enough for an implementation PR to start.
- Unknowns have concrete resolution steps.
- Existing architecture docs still describe only current behavior.
- Dependency docs record current public source locations and commit pins.
- No external concurrency dependency is introduced.
