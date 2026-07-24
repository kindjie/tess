# Concurrency Plan

Status: complete (2026-07-11). This document records the concurrency stream's
decisions and completed rollout for the initial milestone. Later coalescing
maintenance work is tracked by `roadmap-completion.md`. Design intent lives in
the
[concurrent tile-world addendum](../tdd/tdd_addendum_concurrent_tile_world.md)
and the [work contracts addendum](../tdd/tdd_addendum_work_contracts.md);
this plan tracks what has landed and what is deliberately deferred.

## Landed (2026-07-07, S1)

- **Executor contract is public API.** `tess/ops/phase_executor.h` owns the
  backend-facing contract: the `PhaseExecutor` concept
  (`for_each_operation(first, count, fn) -> PlannedExecutionResult`, const,
  all callbacks joined before return), the `SerialExecutor`
  serialized-callback promise, and the documented thread contract
  (non-atomic world metadata, planner-proven disjoint mutable ownership,
  caller-owned dirty partitions reduced in plan order).
- **Persistent worker pool.** `WorkerPoolPhaseExecutor` reuses workers
  across phases with type-erased job publication and allocation-free warm
  dispatch after `reserve_operations`. Serial equivalence is pinned by the
  queued replay stress harness; lifecycle, failure-order, and allocation
  behavior are pinned in `tess_phase_executor_test`. ASan/UBSan and TSan
  clean.
- **Dirty metadata generation protocol.** `World::observe_dirty` /
  `World::clear_dirty_observed` implement the addendum's generation-aware
  observe/clear so future budgeted or concurrent maintenance cannot lose
  marks that land mid-rebuild (see `docs/architecture/storage.md`).
- **Parallel benchmark family.** `bench/tess_parallel_bench.cc` compares
  serial, scoped-thread, and pool backends on identical partitioned
  one-op-per-chunk phases at fixed worker counts, with dispatch-bound
  (`tile_touch`), memory-bound (`chunk_fill`), and compute-bound
  (`chunk_compute`) workloads.

## Evidence

Local M3 Max numbers (dev machine, 256-chunk phase; CI baselines will
supersede these):

- Pool phase dispatch costs ~20 us versus ~40 us for per-phase thread
  creation (scoped-thread) and ~1 us serial. Persistent workers halve
  persistent-pool dispatch cost; per-phase thread creation is confirmed
  non-viable.
- Memory-bound whole-chunk fills (~19 us serial for 256 chunks) do NOT
  benefit from parallel dispatch: the whole phase costs about one dispatch.
  Compute-bound per-chunk work scales: 1.57 ms serial -> 853 us (w2, 1.8x)
  -> 458 us (w4, 3.4x).
- External reference-consumer evidence (bare-metal 192-thread run, reviewed
  2026-07-07): independent per-query work with thread-owned
  state scales to the physical-core knee (~34x at 96 cores); splitting one
  cheap grid pass over a shared buffer anti-scales at every thread count
  (NUMA first-touch plus dispatch). Tess parallelism therefore targets
  per-operation/per-query decomposition with caller-owned scratch and
  planner-proven ownership — never shared-buffer band splits — and any
  future shared-pass parallelism needs a NUMA/first-touch story first.

Implication for consumers: parallel phase execution pays off when
per-operation work is at least several microseconds (weighted path batches,
topology relabels, derived-field rebuilds), not for cheap span fills. The
scheduler stage should route only such work to the pool by default.

## Decisions

- **Write-policy enforcement stays planner-anchored for the initial
  milestone.** Parallel mutation is reachable only through planned phases;
  `plan_parallel_execution_phases` accepts only `ReadOnly` and
  `UniquePerChunk`, separates same-chunk mutable work, rejects
  `UniquePerTile`, and explicit domains are deduplicated at enqueue.
  Policy-typed views enforce `ReadOnly` at compile time, and
  `UniquePerChunk` views are structurally per-chunk. Runtime claim-table
  ownership checking (catching hand-built illegal `ExecutionPhase` values
  under asserts) remains deferred until general queued intents provide a real
  consumer. Planner-issued capability, world, generation, and policy checks
  already reject invalid phases before dispatch.
- **Internal backends before external ones.** Per the addenda,
  `work_contract`/`signal_tree` remain unadopted. The maintenance release will
  evaluate them only behind Tess-owned interfaces against the addendum's
  scheduler criteria; the `PhaseExecutor` surface is identical either way.
- **Parallel benchmark cases are gated by real time.** Ten CI artifacts met
  the calibration precondition on 2026-07-11. Google Benchmark's CPU column
  still measures only the caller thread for pool backends, so pool thresholds
  use real time and retain shared-runner headroom.
- **`ChunkMeta` stays non-atomic** (addendum invariant). Workers write
  per-operation dirty partitions; merges happen on the caller thread. The
  observe/clear generation protocol is the only sanctioned way for
  maintenance to clear dirty state it observed before rebuilding.

## Completed rollout and deferred follow-up

1. **S6 (result channels) — COMPLETE:** result delivery is
   executor-agnostic. Per-operation result slots follow the partitioned-dirty
   pattern, and conformance tests cover result-bearing serial and pool phases.
2. **S7 (scheduler stage, concurrency landing) — OUTCOME (2026-07-10):**
   `WorkerPoolPhaseExecutor` is promoted to the production backend
   (work_contract stays an unadopted experiment; nothing in the S1-S7
   workloads needed its self-scheduling). The scheduler's auto-exec task
   routes phases to an attached pool by operation count, with serial ==
   pool results pinned byte-identical (policy pre-validation makes runtime
   aborts unreachable) and TSan coverage over the schedule + auto-exec
   binaries. Worker counts stay guarded at construction (zero falls back
   to one; no nested dispatch by the single-dispatch guard). DEFERRED beyond
   the initial milestone, with rationale: the coalesced maintenance lane
   (addendum Lane 2) and runtime ownership claim checking — neither has an
   initial-milestone consumer (the only maintenance-shaped consumer keeps its
   edits synchronous), and both belong with the deferred-edit flow that would
   exercise them.
3. **Threshold follow-up — COMPLETE (2026-07-11):** ten CI baseline artifacts
   established the parallel-family real-time thresholds and performance
   trends.

The remaining concurrency work is not unfinished S1-S7 rollout: runtime
ownership claims, cross-thread diagnostic aggregation, and coalescing
maintenance belong to the later queued-execution and maintenance releases in
`roadmap-completion.md`.
