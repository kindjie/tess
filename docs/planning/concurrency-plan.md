# Concurrency Plan

Status: active. This document records the concurrency stream's decisions and
remaining rollout for the v1 completion plan. Design intent lives in the
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
- **Persistent pool prototype.** `WorkerPoolPhaseExecutor` reuses workers
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
  prototype dispatch cost; per-phase thread creation is confirmed
  non-viable.
- Memory-bound whole-chunk fills (~19 us serial for 256 chunks) do NOT
  benefit from parallel dispatch: the whole phase costs about one dispatch.
  Compute-bound per-chunk work scales: 1.57 ms serial -> 853 us (w2, 1.8x)
  -> 458 us (w4, 3.4x).
- External corroboration (kindjie/tile-layout-bench, bare-metal 192-thread
  run, verified 2026-07-07): independent per-query work with thread-owned
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

- **Write-policy enforcement stays planner-anchored in v1.** Parallel
  mutation is reachable only through planned phases;
  `plan_parallel_execution_phases` accepts only `ReadOnly` and
  `UniquePerChunk`, separates same-chunk mutable work, rejects
  `UniquePerTile`, and explicit domains are deduplicated at enqueue.
  Policy-typed views enforce `ReadOnly` at compile time, and
  `UniquePerChunk` views are structurally per-chunk. Runtime claim-table
  ownership checking (catching hand-built illegal `ExecutionPhase` values
  under asserts) is deferred to S7 alongside the production backend, where
  its scratch reservation can be designed with the scheduler's phase
  storage instead of bolted onto `PlannedPhaseExecutionScratch`.
- **Internal backends before external ones.** Per the addenda,
  `work_contract`/`signal_tree` are evaluated only behind Tess-owned
  interfaces after the internal pool has CI baselines. Re-evaluate at S7
  (scheduler stage) against the addendum's maintenance-scheduler criteria;
  the `PhaseExecutor` surface is identical either way.
- **Parallel benchmark cases stay ungated.** Shared-runner scheduling makes
  parallel wall time too noisy to gate, and Google Benchmark's CPU column
  measures only the caller thread for pool backends. Serial cases are the
  gate candidates once CI baselines accumulate, following the existing
  threshold calibration process.
- **`ChunkMeta` stays non-atomic** (addendum invariant). Workers write
  per-operation dirty partitions; merges happen on the caller thread. The
  observe/clear generation protocol is the only sanctioned way for
  maintenance to clear dirty state it observed before rebuilding.

## Remaining rollout

1. **S6 (result channels):** result delivery must be executor-agnostic —
   per-operation result slots follow the partitioned-dirty pattern; the
   conformance suite in `tess_queued_test`/`tess_phase_executor_test`
   extends to result-bearing phases under serial and pool executors.
2. **S7 (scheduler stage, concurrency landing):** promote a production
   phase backend (pool or work_contract, per the S7 evaluation) behind
   `PhaseExecutor`; scheduler auto-exec routes compute-bound phases to it;
   add the coalesced maintenance lane (addendum Lane 2) on top of the
   observe/clear protocol; land runtime ownership claim checking with the
   scheduler's phase storage; add worker-count policy (default, max,
   no nested dispatch).
3. **Threshold follow-up:** after ~5 CI baseline artifacts include the
   parallel family, gate the serial cases and record pool-vs-serial trends
   in `docs/performance.md`.
