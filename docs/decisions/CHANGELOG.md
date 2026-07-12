# Design Changelog

Records meaningful design changes from the original TDDs.

## Template

```md
## YYYY-MM-DD - Title

- Changed:
- Reason:
- Affected docs:
- Affected code:
```

## 2026-07-12 - Tick-engine overhead: schedule, movement, planning (audit3 W3)

- Changed: `Schedule::seal()` builds a phase-major dispatch order (one
  pass per tick instead of SimPhase::Count full scans) and an
  OnDirty-only index that dirty merges write to (only OnDirty cadences
  read `pending_mask`); `run_tick` guards `in_run_` with RAII so a
  throwing task no longer latches the schedule (audit C2, tested).
  Movement validation resolves each endpoint once and threads the
  resolved tiles through passability, occupancy/reservation, version
  checks, commit writes, and dirty marks (was 4-7 resolves per step);
  version checks early-return when the intent carries no expectations.
  `plan_operations` gains a reuse overload planning into a caller-owned
  `ExecutionReport` (rows, planned ops, and pooled chunk lists recycled;
  steady state is allocation-free, tested); `expand_domain` fills
  in place via the collect_* accessors; auto-exec reuses a task-owned
  report. `dirty_axis_end`/`axis_end` now share chunk_meta's saturating
  helper (audit C1); `detail::popcount` is `std::popcount`. Scheduler
  bench medians: empty_tick 50->7 ns, cadence_dispatch_100 510->165 ns,
  dirty_trigger 49->8 ns (local).
- Reason: audit-2026-07-11 M4a/b, M6, M7, C1, C2, and the popcount low
  -- per-tick engine overhead with mechanical fixes.
- Affected docs: `docs/planning/audit-2026-07-11-remediation.md`.
- Affected code: `include/tess/sim/schedule.h`,
  `include/tess/sim/movement.h`, `include/tess/sim/auto_exec.h`,
  `include/tess/sim/render_delta.h`, `include/tess/ops/queued.h`,
  `include/tess/storage/chunk_meta.h`, `tests/tess_sim_schedule_test.cc`,
  `tests/tess_queued_test.cc`.

## 2026-07-11 - Batch grouping, residency probes, settle-target floods (audit3 W2)

- Changed: `weighted_path_batch` scatters shared-goal results through
  counting-sort member buckets (was an O(groups x requests) rescan) and
  hands each field build the group's validated start tiles as settle
  targets -- the goal-rooted flood stops once every consumer start has
  settled instead of exhausting the reachable component (early exit is
  armed only under TreatAsBlocked; an Indeterminate-policy flood must
  still discover missing-chunk boundaries). The batch also verifies the
  field's residency stamp once per group (debug assert) instead of
  recomputing the O(resident_count) fingerprint per member;
  `residency_fingerprint` itself now iterates resident slots directly
  (was 3 directory probes per chunk) and the sparse region-graph
  freshness check reads generation+meta through one probe via the new
  `SparseResidentWorld::resident_ref`. New benches pin the sparse batch
  and near-goal scenarios; the near-goal open-map batch drops ~118x
  (5.79 ms -> 49 us local) while the far-goal multigoal batches are
  unchanged.
- Reason: audit-2026-07-11 M1/M2/M3 -- the batch-pathing tick is the
  hot path; these remove its quadratic member scan, its redundant
  fingerprint traffic, and its whole-map floods for clustered agents.
- Affected docs: `docs/planning/audit-2026-07-11-remediation.md`,
  `docs/planning/optimization-log.md`.
- Affected code: `include/tess/path/path.h`,
  `include/tess/path/detail/weighted_batch.h`,
  `include/tess/storage/sparse_world.h`,
  `include/tess/topology/topology.h`,
  `tests/tess_path_weighted_batch_test.cc`,
  `bench/tess_path_weighted_bench.cc`.

## 2026-07-11 - Bench integrity: de-elision, parallel gates, residency family (audit3 W1)

- Changed: five benchmarks that compiled to empty loops
  (`storage/field_span_acquisition`, `storage/chunk_field_write_read_iteration`,
  `storage/single_chunk_page_iteration`, `storage/flat_array_iteration`,
  `block/scratch_allocate_u32`) and one partially-elided one
  (`diagnostics/record_timing`) now measure real work via
  escape-then-clobber and opaque-input patterns; their ceilings are
  re-set (bootstrap x6-local for the three loop benches, 25 ns floor for
  the sub-ns ones) pending the 10-artifact recalibration. The
  `parallel/` family is now gated (`bench/thresholds/parallel.json`,
  real_time ceilings from 10 CI artifacts -- the deferred precondition
  was met). Threshold targets gate on the median of
  `TESS_BENCHMARK_GATE_REPETITIONS` (default 3) repetitions instead of a
  single unreplicated sample. New ungated `residency/` family
  (`bench/tess_residency_bench.cc`) covers sparse lookup,
  ensure_resident hits, and eviction churn at budget -- the baseline
  evidence for the audit M11b LRU fix.
- Reason: audit-2026-07-11 H1 and bench lows -- gates that measure
  nothing protect nothing, and later remediation workstreams need
  trustworthy before/after numbers.
- Affected docs: `docs/planning/audit-2026-07-11.md`,
  `docs/planning/audit-2026-07-11-remediation.md`.
- Affected code: `bench/tess_bench.cc`, `bench/tess_diagnostics_bench.cc`,
  `bench/tess_residency_bench.cc`, `bench/CMakeLists.txt`,
  `bench/thresholds/{storage,block,parallel}.json`,
  `.github/workflows/ci.yml`.

## 2026-07-11 - v0.1.0 (S11 close)

- Changed: project version 0.1.0 -> 0.1.0 (CMake `project(VERSION)`,
  `TESS_VERSION_*` macros, and the smoke test's pinned literals bumped
  together). The v1 milestone plan is stamped COMPLETE and preserved as
  the planning record; the architecture README now describes the shipped
  v1 surface. The consumer's composite 10k-tick soak (its S11.4 test)
  locks the integrated behavior the acceptance criteria describe.
- Reason: S11.6 -- every milestone M0-M15 shipped with its gates,
  documentation, and consumer adoption in place.
- Affected docs: `docs/planning/v1-milestone-plan.md`,
  `docs/architecture/README.md`.
- Affected code: `CMakeLists.txt`, `include/tess/tess.h`,
  `tests/tess_smoke.cc`.

## 2026-07-11 - Threshold recalibration + trends snapshot (M14, S11.3)

- Changed: every gated benchmark ceiling in `bench/thresholds/` for the
  key, storage, block, queued, path, topology, and diagnostics families
  is now twice the maximum observed across ten main-run CI baseline
  artifacts (runs 29056942917-29167134881, 10 repetitions each),
  tightened from the single-artifact 3x policy per the 10-artifact rule
  in `docs/performance.md`; 2x headroom absorbs the shared-runner pool's
  heterogeneous-CPU spread, and nanosecond-scale gates keep an absolute
  25 ns floor (2x-of-observed below that fails a correct benchmark on a
  merely-slower runner SKU -- observed empirically during review; these
  gates exist for 5-100x gross regressions). The trends snapshot
  (`docs/assets/benchmark-trends.svg` + the `docs/performance.md` table)
  is regenerated from the same ten artifacts.
- Fixed: the `tess_bench_ci_baselines` target had never been extended
  past the diagnostics family -- scheduler (S7), ecs (S8), render-delta
  (S9), and fields (S11.1) gates existed with no baseline collection.
  They are wired in now (through the binaries their threshold targets
  use), keep their bootstrap ceilings, and get recalibrated once enough
  artifacts carrying them accumulate.
- Reason: S11.3 (consolidation) -- the deferred tightening pass and the
  deferred >=5-artifact snapshot regeneration, done once, reviewably.
- Affected docs: `docs/performance.md`,
  `docs/assets/benchmark-trends.svg`.
- Affected code: `bench/thresholds/{key-conversions,storage,block,
  queued,path,topology,diagnostics}.json`, `bench/CMakeLists.txt`.

## 2026-07-11 - Consolidation examples + CI example smoke (M15, S11.2)

- Added: three examples closing the M15 checklist.
  `examples/colony_2d.cc` is the flagship composition -- queued
  construction edits through an `AutoExecTask` in the PreUpdate phase,
  an OnDirty Topology-phase task doing the incremental per-class region
  update and re-path, movement-class (`MovementClass` walker) agents
  routing around the wall the ops built, and a `DeltaCollector`
  publishing versioned render frames, all under one `tess::Schedule`
  driven by `run_schedule_frame`. `examples/ant_farm_vertical.cc` runs
  a degenerate-axis (y extent 1) x-z cross-section world: one
  distance-field product flooded from all food chambers, per-ant
  `nearest_target` descents, and the shared product served from the
  `FieldProductCache` (asserted hits). `examples/stairs_3d.cc` shows
  `StairTransitions` connecting two z-levels that share no passable
  face, the precheck agreeing, and an incremental `update_region_graph`
  severing the link after demolition. Every example is self-checking
  (nonzero exit on violated expectations), and the dev CI job gains an
  "Example smoke" step that executes every built example binary and
  asserts the expected count ran.
- Reason: S11.2 (consolidation) -- v1's example checklist and a CI
  guarantee that examples keep running, not just compiling.
- Affected docs: `README.md` (stale "two examples" paragraph replaced
  with the full list).
- Affected code: new `examples/colony_2d.cc`,
  `examples/ant_farm_vertical.cc`, `examples/stairs_3d.cc`;
  `examples/CMakeLists.txt`, `.github/workflows/ci.yml`.

## 2026-07-11 - Fields benchmark family (M9/M14, S11.1)

- Added: `bench/tess_fields_bench.cc` + thresholds + the CI step -- the
  gated family M9 never had: distance-field product builds scaling with
  the goal count (1/16/256 goals over an open 64x64 world; 85-103 us
  local), the nearest-target gradient query over a built product
  (64 ns), FieldProductCache hit (25 ns), the mixed miss+store steady
  state, byte-budgeted LRU eviction under a cycling working set, and
  the warm-build allocation gate (zero allocations into reserved
  product/scratch storage; family runs through the diagnostics binary).
  Stateful correctness checks are guarded by iteration count because
  the harness's one-iteration calibration pass runs them too.
- Reason: S11.1 (consolidation) -- closes the M14 family checklist.
- Affected docs: none.
- Affected code: new `bench/tess_fields_bench.cc`,
  `bench/thresholds/fields.json`, `bench/CMakeLists.txt`,
  `.github/workflows/ci.yml`.

## 2026-07-11 - GPU backend interface, interface only (M13, S10)

- Added: `include/tess/gpu/descriptors.h` (GpuFieldFormat derived from
  schema value types; FieldMirrorDesc/`field_mirror_desc` computed from
  compile-time layout facts -- tess pages are SoA per chunk, so mirrors
  are chunk-key-major slices; UploadDesc/`upload_desc` staging a live
  chunk span; DispatchDesc; explicit ReadbackPolicy/ReadbackDesc with no
  full readback by default) and `include/tess/gpu/backend.h`
  (GpuCapabilities, the compile-time-polymorphic GpuBackend concept
  with bool-refusal semantics and CPU fallback, and the default
  NoGpuBackend that compiles everywhere and refuses everything). The
  test-only MockGpuBackend records call sequences. Benchmarks are
  deliberately absent: nothing executes in v1 (per the plan's
  "ungated smoke only" note, resolved as no-bench + this record).
- Reason: M13 -- a real backend can be added later without redesigning
  core; CPU-only builds carry zero GPU obligations.
- Affected docs: new `architecture/gpu.md` (+ README index),
  `surface.json`, `tests/AGENTS.md`.
- Affected code: new `gpu/backend.h`, `gpu/descriptors.h`, `tess.h`,
  `CMakeLists.txt`; new `tests/gpu_mock_backend.h`,
  `tests/tess_gpu_interface_test.cc`, `tests/CMakeLists.txt`.

## 2026-07-11 - Render-delta bench family and headless consumer (M11, S9.5)

- Added: `bench/tess_render_delta_bench.cc` + thresholds + the CI step:
  sparse-tile collection (64 scattered tiles over 4096 chunks, 2.3 us
  local), box-granular collection (2.4 us), entity recording at 1k
  entities x 8 steps coalesced vs per-step (24 vs 40 us -- the
  coalescing ratio is visible as trend), the full 4096-chunk baseline
  (14 us), and the allocation gate (zero allocations in a steady
  mark/collect/record/publish cycle; the family runs through the
  diagnostics binary so the gate executes in CI). Bootstrap ceilings
  ~10x local pending the consolidation recalibration. Also
  `examples/render_delta_consumer.cc`: a headless shadow-grid consumer
  that late-joins through a baseline, deliberately drops a frame,
  detects the version gap, and resyncs -- the honest end-to-end home of
  the gap/baseline protocol.
- Reason: S9.5 (M11 close on the tess side).
- Affected docs: none beyond prior slices.
- Affected code: new `bench/tess_render_delta_bench.cc`,
  `bench/thresholds/render-delta.json`, `bench/CMakeLists.txt`,
  `.github/workflows/ci.yml`; new `examples/render_delta_consumer.cc`,
  `examples/CMakeLists.txt`.

## 2026-07-11 - Replay validator and randomized replay acceptance (M11, S9.4)

- Added: `tests/render_delta_replay.h` -- the consumer-model
  RenderReplayGrid (invalidation apply that re-reads the current world
  for covered tiles, a shadow entity->tile map fed by entity deltas,
  the version contract enforced exactly as a consumer would, baselines
  clearing shadow + entity state with an explicit re-snapshot seam).
  Randomized script tests pin the section-8 acceptance "delta replay
  matches projected state": per-tick consumption, coalesced eight-tick
  frames, a lossy consumer reconverging through gap detection + full
  baseline, and a sparse resident-set replay.
- Reason: S9.4 (M11).
- Affected docs: `tests/AGENTS.md`.
- Affected code: new `tests/render_delta_replay.h`;
  `tests/tess_render_delta_frame_test.cc`.

## 2026-07-11 - Baselines, applicability hardening, path overlays (M11, S9.3)

- Added: `collect_baseline` (full scope ONLY -- scoped baselines are
  deliberately absent: a partial baseline adopting the frame version
  would permanently lose out-of-scope invalidations from a gap);
  `PathOverlayDelta` + `stage_path_overlay` + `collect_path_overlays`
  (full-replacement per-frame route decorations, nodes copied at call
  time, gated on `has_goal && Found` to provably avoid stale-ticket
  asserts; overlay overflow drops the overlay, never the frame).
  Changed: `delta_frame_applicable` now rejects truncated BASELINES too
  -- one that overflowed chunk storage covers only part of the world
  while claiming full sync, so baseline consumers size chunk capacity
  to the world and the truth table pins the rejection.
- Reason: S9.3 (M11).
- Affected docs: `architecture/simulation.md`, `surface.json`,
  `tests/AGENTS.md`.
- Affected code: `sim/delta_frame.h`;
  `tests/tess_render_delta_frame_test.cc`.

## 2026-07-11 - Entity-delta hook seam through the ECS pipeline (M11, S9.2)

- Added: a trailing defaulted `DeltaCollector*` on
  `advance_path_agents_with_index`, all `tick_ecs_*`/`tick_entt_*`
  drivers (which stamp `begin_tick` with the new tick before movement),
  and the EnTT lifecycle intents (recording Spawned/Despawned/
  Teleported/Parked/Placed exactly when the intent succeeds; a parked
  despawn records nothing because parking already released its tile).
  The movement observer records each committed step beside the index
  move, so entity-delta completeness holds for the sanctioned ECS
  surface by construction. Source-compatible: all params default to
  nullptr, and a positional `graph` argument cannot bind to the
  collector (distinct pointer types).
- Reason: S9.2 (M11) -- entity deltas are pushed at commit, never
  diffed from mirrors.
- Affected docs: `architecture/ecs.md`, `tests/AGENTS.md`.
- Affected code: `ecs/adapter.h`, `ecs/entt/entt_adapter.h`;
  `tests/tess_ecs_adapter_test.cc`, `tests/tess_ecs_entt_test.cc`.

## 2026-07-11 - DeltaFrame render bridge core (M11, S9.1)

- Added: `include/tess/sim/delta_frame.h` -- the versioned frame
  protocol. Tile deltas are INVALIDATION records (no field values;
  consumers re-read the current world at apply, idempotent convergence);
  collection happens once per published frame through the lost-update-
  safe observe/clear-observed protocol, so multi-tick tile coalescing is
  the storage's native union semantics, free. Per-chunk encoding:
  per-tile records up to `sparse_tile_threshold`, one clipped box record
  above it (and as the degradation when tile storage cannot hold a
  chunk -- never a truncation). Entity records (Moved coalescible
  last-writer-wins within a frame; Teleported/Spawned/Despawned/Parked/
  Placed are barriers) with the coalescing map cleared by walking the
  frame's records (O(records), probe chains kept by backward-shift
  erase). Versioning: collectors start at 1; 0 is the fresh-consumer
  echo so late joiners can only start from a baseline; the version bumps
  iff a frame carries state; truncation (capacity or a hard `clear()`,
  which poisons the stream until a baseline) is a STRUCTURAL gap in
  `delta_frame_applicable`, never advisory. Also hoisted `EntityHandle`
  into `include/tess/ecs/entity_handle.h` so the bridge names entity
  identity without the ECS pipeline include.
- Reason: S9.1 (M11 close), per the reviewed design: the pre-review
  version-0 late-join hole, scoped-baseline gap hazard, silent clear(),
  and advisory-truncation findings are all closed structurally.
- Affected docs: `architecture/simulation.md` (DeltaFrame section),
  `surface.json`, `tests/AGENTS.md`.
- Affected code: new `sim/delta_frame.h`, new `ecs/entity_handle.h`,
  `ecs/adapter.h`, `tess.h`, `CMakeLists.txt`; new
  `tests/tess_render_delta_frame_test.cc`, `tests/CMakeLists.txt`.

## 2026-07-11 - Ecs benchmark family (M10/M14, S8.6)

- Added: `bench/tess_ecs_bench.cc` + `bench/thresholds/ecs.json` + the CI
  threshold step: AgentId-sorted collect over churn-scrambled pools at
  1k/10k/100k (11/104/1092 us local), write-back at 1k/10k (7/75 us),
  the occupancy-index move hot path (9.4 ns), and the adapter-overhead
  headline -- the full EnTT tick vs a raw PathAgentState span doing
  identical marching-agent work (1k: 86 vs 12 us; 10k: 807 vs 140 us
  local; the ratio is collect+sort+write-back+index maintenance and is
  trend-only, never gated). Steady-state ticks are timed with resets and
  re-path ticks absorbed outside the timed region. The family runs
  through the diagnostics bench binary so `ecs/tick_entt_alloc_gate`
  (aborts unless a steady-state tick reports zero allocations,
  benchmark-plan section 14) executes in CI. A separate
  movement-commit-only case was folded into the tick pair -- quiet ticks
  are movement-dominated already. Bootstrap ceilings pending CI
  recalibration in the consolidation stage.
- Affected docs: none.
- Affected code: new `bench/tess_ecs_bench.cc`,
  `bench/thresholds/ecs.json`, `bench/CMakeLists.txt`,
  `.github/workflows/ci.yml`.

## 2026-07-11 - ECS examples: custom store + EnTT pawns (M10/M15, S8.5)

- Added: `examples/custom_ecs_min.cc` (always built) -- a self-contained
  hand-rolled store (parallel arrays, generational ids, the game's own
  position component) implementing every adapter concept and driving the
  generic `tick_ecs_*` pipeline to arrival, proving the concepts are not
  EnTT-shaped; and `examples/entt_pawns.cc` (behind `TESS_ENABLE_ENTT`)
  -- pawn entities spawned through the lifecycle intents, a mid-flight
  goal reassignment, and a despawn freeing its tile, ending with an index
  sync check. Covers M15's "EnTT pawn movement" and "custom adapter"
  example items early.
- Affected docs: none (examples are self-documenting).
- Affected code: new `examples/custom_ecs_min.cc`,
  `examples/entt_pawns.cc`, `examples/CMakeLists.txt`.

## 2026-07-11 - EnTT adapter (M10, S8.4)

- Added: `include/tess/ecs/entt/entt_adapter.h` (gated by the
  consumer-side `TESS_ENABLE_ENTT` macro + an `ENTT_VERSION` `#error`
  enforcing include-before): `EnttHandleAdapter` (null special-cased both
  directions -- entt's null zero-extends to a non-null handle value),
  `EnttTilePositionAdapter` (default PositionAdapter; games substitute
  their own), `EnttPathAgentContext`, `EnttPathAgentSource` (AgentId-
  sorted collection: entries sorted FIRST, batch filled in sorted order;
  PathGoal reconciled into the lifecycle with Unreachable-stays-terminal
  semantics), `EnttPathAgentSink` (idempotent mirror; PathGoal consumed
  on arrival), lifecycle intents (spawn / off-board spawn / despawn /
  teleport / park / place / set / clear goal -- the only sanctioned
  mutation paths; teleport RETAINS PathGoal and re-arms from the new
  position), and `tick_entt_*` drivers that are thin instantiations of
  the generic `tick_ecs_*` pipeline (one pipeline, not two).
- Reason: M10. Off-board (`OffBoard` park/place) exists because the
  downstream consumer parks unplaceable agents after world edits;
  section-8 sync invariants are scoped to on-board agents.
- Affected docs: `architecture/ecs.md` (EnTT section), `surface.json`,
  `tests/AGENTS.md`.
- Affected code: new `ecs/entt/entt_adapter.h`, `CMakeLists.txt`; new
  `tests/tess_ecs_entt_test.cc`, `tests/CMakeLists.txt`.

## 2026-07-10 - EnTT dependency wiring (M10, S8.3)

- Added: `cmake/TessEnttDeps.cmake` pinning EnTT `v3.16.0` at the
  downstream consumer's known-good, MSVC-exercised SHA (also the latest
  upstream tag as of 2026-07-10); `tess_require_entt()` prefers an
  existing `EnTT::EnTT` target or `find_package`, then FetchContent
  (`SYSTEM`, `EXCLUDE_FROM_ALL`). New CMake option `TESS_ENABLE_ENTT`
  (default OFF so network-free builds never fetch; ON in the dev,
  release, bench, bench-profile, and windows-msvc presets) gates only
  tess's own EnTT-dependent test/example/bench targets -- the adapter
  header itself stays gated by the same-named consumer-side preprocessor
  macro, the ImGui precedent.
- Reason: M10 build policy -- core stays free of third-party CMake
  surface; the pin pairs with the consumer's and upgrades in lockstep
  only.
- Affected docs: `docs/dependencies.md` (new EnTT section).
- Affected code: new `cmake/TessEnttDeps.cmake`, `CMakeLists.txt`,
  `CMakePresets.json`.

## 2026-07-10 - ECS-agnostic adapter layer (M10, S8.2)

- Added: `include/tess/ecs/adapter.h`, the dependency-free ECS layer:
  `EntityHandle` (+ `kNullEntityHandle`), the `EntityHandleAdapter` /
  `PositionAdapter` / `PathAgentSource` / `PathAgentSink` concepts, shared
  POD components (`AgentId`, `TilePosition`, `PathGoal`, `PathState`,
  `OffBoard`), `PathAgentBatch` SoA scratch, `TileOccupancyIndex`
  (injective tile->entity open-addressing map with backward-shift
  deletion; box/radius queries deferred post-v1 -- probing every box
  coordinate is not a useful spatial query and `entity_at` is the
  primitive), `advance_path_agents_with_index` over the S8.1 commit
  observer, and the `tick_ecs_*` pipeline (collect -> dirty-gated
  exactly-once processing -> index-synchronized movement -> apply).
- Reason: M10. The seam is "agents in deterministic order in, state
  write-back out": adapters mirror lifecycle state instead of
  re-implementing tickets/retries/result application. Determinism
  contract: sources sort by monotonic `AgentId`, never native entity
  value or pool order. The runtime is exclusive to the agent system
  (tickets persist in components across quiet ticks).
- Affected docs: new `docs/architecture/ecs.md` (+ README index),
  `surface.json`, `tests/AGENTS.md`.
- Affected code: new `ecs/adapter.h`, `tess.h`, `CMakeLists.txt`; new
  `tests/tess_ecs_adapter_test.cc`, `tests/CMakeLists.txt`.

## 2026-07-10 - Movement-advance commit observer (M10, S8.1)

- Changed: `advance_path_agents_with_movement` gained an observer overload
  taking `on_commit(agent_index, from, to)`, invoked once per successful
  `commit_movement_intent` -- after the agent position and occupancy fields
  are updated, before arrival handling -- and never for failed validations
  (which write nothing). The existing form delegates with a no-op.
- Reason: the M10 ECS adapter must keep an external tile->entity occupancy
  index synchronized with every committed step (cross-cutting acceptance
  section 8). An observer at the exact commit point keeps one lifecycle
  implementation, preserves the failure taxonomy in one place, and avoids
  the alternatives -- duplicating the advance loop in the adapter or O(n)
  position diffing that loses per-step attribution.
- Affected docs: `docs/architecture/simulation.md` (S8.2 will add the ecs
  doc that consumes this).
- Affected code: `sim/path_agent.h`; `tests/tess_path_agent_test.cc`.

## 2026-07-10 - Codex review fixes: EveryN pokes, hook follow-ups (M5, S7)

- Fixed: two of three connector findings (the third -- the PolicyMismatch
  clear -- had already landed in the pre-merge hardening commit).
  (1) `request_run` now arms EveryN tasks too, as its contract says: one
  extra run without shifting the countdown's lockstep phase (the poke was
  previously ignored and silently consumed at the next scheduled fire).
  (2) The auto-exec task clears its queue BEFORE draining, so follow-up
  operations a result hook enqueues land in the fresh queue for the next
  run instead of being discarded by the end-of-run clear; the channel
  still clears after the drain, completing the paired-clear discipline.
  Both pinned by regression tests.
- Affected docs: none beyond header comments.
- Affected code: `sim/schedule.h`, `sim/auto_exec.h`;
  `tests/tess_sim_schedule_test.cc`, `tests/tess_sim_auto_exec_test.cc`.

## 2026-07-10 - Pre-merge review hardenings: mismatch drop, cadence clamps, reentrancy guard (M5, S7)

- Fixed: three review findings. (1) The auto-exec PolicyMismatch path now
  drops the queue (paired clears): keeping it would wedge the task forever
  in release builds, rescanning the same poisoned frame while new enqueues
  pile on. (2) `Schedule::add_task` validates and re-clamps hand-built
  cadences (Cadence is a public aggregate, so a zero EveryN bypassing the
  factory clamp would wrap its countdown to ~4.29B ticks, and a zero
  background budget would spin in_progress forever). (3) run_tick carries
  an in-run reentrancy assert, and the header names the two calls task
  bodies must not make (add_task, nested run_tick) alongside the three
  that are safe. Also documented: ChunkFn is shared across pool workers
  and must be safe for concurrent invocation; an OnDirty task poked via
  request_run receives pending_dirty == 0 (a full-run request, pinned by
  test).
- Reason: pre-merge review of the S7 branch (no blockers; these were the
  should-fixes and note-level hardenings).
- Affected docs: none beyond header comments.
- Affected code: `sim/schedule.h`, `sim/auto_exec.h`;
  `tests/tess_sim_schedule_test.cc`.

## 2026-07-10 - Scheduler bench family (M5, S7 slice 6)

- Added: `bench/tess_scheduler_bench.cc` + `bench/thresholds/scheduler.json`
  + the CI threshold step: empty-tick dispatch floor (47 ns local),
  100-task cadence dispatch (506 ns), the dirty-trigger path (46 ns), and
  the full auto-exec pipeline per tick over a 64-chunk resident update
  (564 ns). Bootstrap ceilings pending CI recalibration; parallel speedups
  stay trend-only per the standing bench policy.
- Reason: S7 slice 6 -- the plan's scheduler bench family.
- Affected docs: none.
- Affected code: `bench/tess_scheduler_bench.cc`, `bench/CMakeLists.txt`,
  `bench/thresholds/scheduler.json`, `.github/workflows/ci.yml`.

## 2026-07-10 - Worker pool promoted to the production backend (M5, S7 slice 5)

- Changed: docs only. `WorkerPoolPhaseExecutor` is recorded as the
  production parallel backend (the S7 evaluation outcome the concurrency
  plan called for): auto-exec routes phases to it by operation count,
  serial == pool is pinned byte-identical, and TSan covers the schedule and
  auto-exec binaries. work_contract remains an unadopted experiment. The
  coalesced maintenance lane and runtime ownership claim checking are
  explicitly deferred post-v1 with rationale (no v1 consumer; they belong
  with a deferred-edit flow).
- Reason: S7 slice 5 -- the concurrency stream's landing record.
- Affected docs: `architecture/queued-operations.md`,
  `planning/concurrency-plan.md`.
- Affected code: none.

## 2026-07-10 - Auto-exec task with per-phase routing and goldens (M5, S7 slices 3-4)

- Added: `include/tess/sim/auto_exec.h` -- `AutoExecTask` runs the whole
  queued-ops pipeline as one schedule task over a caller-owned FrameOps
  queue: plan, parallel phase planning, execution serial or on the worker
  pool (chosen per phase by op count), dirty merge after EACH phase (the
  partitioned scratch is re-prepared per phase; a post-loop merge would
  drop all but the last phase's records), ack drain through the task's
  result hook, and paired queue+channel clears ending every run. Policy
  uniformity is pre-validated so runtime aborts are unreachable, which is
  what makes serial and pool execution provably identical.
- Goldens (slice 4): auto-exec == the hand-rolled plan/execute/merge
  pipeline (whole-world fields, chunk versions, dirty flags); serial ==
  pool (worlds, metadata, drained ack sequences) with pool phases taken;
  both binaries green under the TSan preset.
- Reason: S7 slices 3-4 (M5 close): the plan->execute->dirty-apply->drain
  requirement with the S1-validated pool as a per-phase production choice.
- Affected docs: `architecture/simulation.md`, `architecture/surface.json`,
  `tests/AGENTS.md`.
- Affected code: new `sim/auto_exec.h`, `tess.h`, `CMakeLists.txt`; new
  `tests/tess_sim_auto_exec_test.cc`, `tests/CMakeLists.txt`.

## 2026-07-10 - Schedule frame driver (M5, S7 slice 2)

- Added: `run_schedule_frame` + `ScheduleFrameSummary` -- the frame-to-ticks
  bridge over `FixedStepAccumulator`, running the schedule once per granted
  fixed tick so every cadence counts sim ticks rather than frames (exact
  under SimSpeed changes, backlog, and pause; pinned by test). The design's
  proposed task-adapter header was cut: the auto-exec golden composes
  test-local tasks, and the downstream consumer's agent tick is its own
  task body, so no adapter had a consumer.
- Reason: S7 slice 2 (M5 close).
- Affected docs: `architecture/simulation.md`, `architecture/surface.json`,
  `tests/AGENTS.md`.
- Affected code: `sim/schedule.h`; `tests/tess_sim_schedule_test.cc`.

## 2026-07-10 - Schedule core: phases, cadences, budgets (M5, S7 slice 1)

- Added: `include/tess/sim/schedule.h` -- the M5 schedule. Ordered
  `SimPhase` execution of type-erased tasks (fn-pointer + context, no
  std::function); cadences EveryTick / EveryN (exact under disablement:
  the countdown advances regardless, so re-enabling never shifts lockstep)
  / OnDirty (fires iff the task's OWN mask bits are pending; consumes only
  those bits) / Background (deterministic items-only budget with more_work
  continuation; a wall-clock valve was cut deliberately -- it would make
  tick outcomes nondeterministic and had no v1 consumer) / Manual.
  Task-result dirty masks merge immediately (later phases fire same tick,
  earlier next tick); notify_dirty/request_run are frame-owner-thread
  only. `SimClock` hoisted from the path-agent tick header into `time.h`
  so both layers share one type. Dispatch after seal() is allocation-free.
- Reason: S7 slice 1 (M5 close). The design review's determinism fixes are
  folded in from the start: EveryN countdown initialized to n (no
  first-tick underflow), own-bit-only OnDirty clearing, and dirty-feeding
  (never scanning) as the structural no-full-world-scans guarantee.
- Affected docs: `architecture/simulation.md`, `architecture/surface.json`,
  `tests/AGENTS.md`.
- Affected code: new `sim/schedule.h`, `sim/time.h` (SimClock hoist),
  `sim/path_agent_tick.h`, `tess.h`, `CMakeLists.txt`; new
  `tests/tess_sim_schedule_test.cc`, `tests/CMakeLists.txt`.

## 2026-07-10 - Result-bearing queued bench gate (M4, S6 slice 4)

- Added: `queued/execute_resident_update_with_results` -- the resultless
  resident-update workload through
  `execute_plan_deferred_dirty_with_results` plus the per-frame drain and
  channel clear a consumer performs, with a correctness check that exactly
  one ack is delivered per frame. Gated at 1500 ns CPU next to the 880 ns
  resultless ceiling (locally 140 vs 114 ns, ~23% ack-delivery overhead);
  bootstrap threshold pending CI recalibration in the consolidation stage.
- Reason: S6 slice 4 -- the plan's bounded-overhead requirement for
  result-bearing versus resultless plan+execute.
- Affected docs: none.
- Affected code: `bench/tess_bench.cc`, `bench/thresholds/queued.json`.

## 2026-07-10 - Result-bearing execution wrappers (M4, S6 slice 3)

- Added: `execute_phase_partitioned_dirty_with_results<Policy>` and
  `execute_plan_deferred_dirty_with_results<Policy>` in
  `ops/result_channel.h`. The callback gains the operation's channel value
  (`fn(view, T&)`), accumulated op-exclusively on the executing thread;
  completions are stamped worker-side because `PlannedExecutionResult`
  default-constructs to Executed and the serial executor early-stops, so a
  post-barrier sweep would misread never-run operations as executed. All
  phase/plan operations are prepared upfront (aborted tails read Pending),
  and the phase range is validated before the channel is touched.
- Reason: S6 slice 3 -- the executor-agnostic delivery path: identical
  drain order and content under serial and threaded executors for
  successful plans (pinned by test against both), failure reasons instead
  of values at runtime, allocation-free warm frames.
- Affected docs: `architecture/queued-operations.md`,
  `architecture/surface.json`, `tests/AGENTS.md`.
- Affected code: `ops/result_channel.h`, `ops/queued.h` (one friend
  declaration + a ResultChannel forward declaration);
  `tests/tess_queued_results_test.cc`.

## 2026-07-10 - PlannedOperation carries its enqueue source (M4, S6 slice 2)

- Changed: `PlannedOperation` gains a trailing `std::source_location source`
  member, copied from the queued operation at plan time, so run-time
  completions can report the enqueue site exactly as plan-time rejections
  already do through `OperationReport::source`. Behavior-neutral otherwise;
  the only aggregate-init site is `plan_operations`.
- Reason: S6 slice 2 -- the result-bearing execute wrappers (next slice)
  stamp `OpCompletion.source` from the planned operation on the executing
  thread; without this member they would need the full `ExecutionReport`
  plumbed through, which breaks for subset-span plans.
- Affected docs: none (member documented inline; not a surface symbol).
- Affected code: `ops/queued.h`.

## 2026-07-10 - Result-channel core: OpCompletion + drain-only ResultChannel (M4, S6 slice 1)

- Added: `include/tess/ops/result_channel.h` -- `OpCompletion` (both failure
  domains plus chunk count and enqueue-site source_location, with a
  `completed` flag so a default record can never read as success),
  `OpResultState` (Unbound/Pending/Ready/Failed), the caller-owned dense
  `ResultChannel<T>` keyed by OpHandle with `drain_results(visitor)` in
  handle order (failures deliver reasons, never values; drain-once;
  lookups readable until clear), and `record_plan_completions(report,
  channel)` delivering plan-time rejections through the same drain.
- Reason: S6 slice 1 (M4 close). Design review (workflow + two adversarial
  critiques) converged on a deliberately DRAIN-ONLY v1: the pipeline has no
  asynchronous execution path, so a poll-only future could never be
  observed pending and was the source of every footgun found (stale-assert
  semantics, fail-safe fallbacks, dead expect()); futures return when
  budget-deferred execution gives them a consumer. Recorded as a TDD
  divergence alongside the deferred cancelled/superseded states.
  Publication is executor-agnostic with zero atomics: per-op slot writes on
  the executing thread, all reads after the synchronous execute call, join
  barrier supplies visibility.
- Affected docs: `architecture/queued-operations.md`,
  `architecture/surface.json`, `tests/AGENTS.md`.
- Affected code: new `ops/result_channel.h`, `tess.h`, `CMakeLists.txt`;
  new `tests/tess_queued_results_test.cc`, `tests/CMakeLists.txt`.

## 2026-07-10 - Codex review fixes: span-path advertisement, stair narrowing, direct route-cache guard (M6, S5)

- Fixed: three of four connector-review findings. (1) `WalkableCostField`
  declared `passable_tag`, satisfying `HasPassableSpan` without providing
  `passable_span`, so using it for region labeling failed to compile inside
  the topology flood; the marker is removed (its two-field predicate must
  not take the raw-span fast path) and the concept's contract is documented.
  (2) `StairTransitions` narrowed the stair value to `uint8_t` before the
  range check, so a wider field holding 257 wrapped into `PositiveX`; the
  check now precedes any narrowing and rejects negatives. (3) The public
  `cached_astar_path` accepts movement classes but `RouteCacheScratch` keys
  entries on (start, goal) only, so DIRECT callers (outside
  `PathRequestRuntime`'s binding) could be served another class's route; the
  cache now binds itself to each call's normalized class and a rebind drops
  the entries, counted in the new `RouteCacheStats::class_rebinds`.
- Declined: validating entry cost in `validate_movement_intent`. Commit
  validates the class's PASSABILITY predicate only -- cost is a search
  concern, and commit staying more permissive than the weighted search is
  the deliberate legacy asymmetry (`WalkableCostField` is the opt-in that
  folds cost into passability at commit too). Documented in simulation.md.
- Affected docs: `architecture/simulation.md`, `tests/AGENTS.md`.
- Affected code: `topology/movement_class.h`,
  `topology/transition_provider.h`, `path/route_cache.h`;
  `tests/tess_topology_movement_test.cc` (WalkableCostField labeling
  compiles and excludes zero-cost tiles; wide stair value does not wrap),
  `tests/tess_path_movement_class_test.cc` (direct route-cache class guard).

## 2026-07-10 - Pre-merge review fixes: stair down-transitions, class-binding order (M6, S5)

- Fixed: two defects found (and reproduced) by the pre-merge review.
  (1) `StairTransitions` never emitted the DOWN transition of a stair whose
  landing steps sideways across an x/y chunk boundary at a local z below the
  chunk top: enumeration scanned only the chunk itself and its -z neighbor.
  It now scans the four sideways same-z neighbors too, so every legal
  landing chunk (own, +z, or sideways face neighbor) emits its down
  direction. (2) `PathRequestRuntime::process_unit_cached` bound the
  movement class BEFORE `prepare_process`, so a policy-triggered
  `clear_caches()` (e.g. `clear_every_world_change = 1`) zeroed the binding
  mid-call and the caches refilled under an unbound identity a later class
  could silently reuse; the binding now happens after the prepare pass.
  Also: an out-of-range stair field value now reads as `None` instead of
  leaking an unintended straight-up transition, the `bind_unit_class` and
  path.md docs now state explicitly that the caller-driven weighted portal
  segment cache is NOT covered by the class binding (keep one per class,
  as before), and topology.md notes the sparse reaches-missing pass
  re-enumerates every resident chunk per update.
- Reason: both defects lived exactly in the gaps the original fixtures
  could not reach (multi-z-extent chunks; the `clear_every_world_change`
  knob) -- each is now pinned by a regression test.
- Affected docs: `architecture/path.md`, `architecture/topology.md`,
  `tests/AGENTS.md`.
- Affected code: `topology/transition_provider.h`, `path/path_runtime.h`;
  `tests/tess_topology_movement_test.cc` (sideways-crossing stair both
  directions + incremental equality, out-of-range stair value),
  `tests/tess_path_movement_class_test.cc` (policy-clear rebind guard).

## 2026-07-10 - StairTransitions vertical provider (M6, S5 slice 8)

- Added: `StairTransitions<StairTag>` in `topology/transition_provider.h` --
  an integral field holds a `StairDirection`, marking the tile as the foot
  of a stair whose landing is one step sideways and one z-level up. The
  offset form is deliberate: two stacked passable tiles are already six-axis
  adjacent, so only an offset stair adds connectivity. Both directions are
  emitted, each from the chunk owning its origin tile (down from the
  landing's chunk -- the foot's chunk or its +z face neighbor), keeping
  incremental re-derivation exact. Endpoint traversability stays a
  movement-class question (label filter), so stair edges are per-class. A
  landing that would cross two chunk boundaries at once (sideways off the
  x/y edge AND up off the top z layer) violates the face-neighbor contract
  and contributes nothing, documented as the placement limit. On sparse
  worlds a foot in a non-resident chunk needs no special handling: the
  landing tile's own -z boundary exit already marks its region as reaching
  missing topology.
- Reason: S5 slice 8 -- the concrete vertical provider closing M6's
  stairs/ladders deliverable on the S5.7 contract, with no byte-identity
  shield (new code), so connectivity, per-class filtering, incremental
  equality, and the diagonal limit are all pinned by property tests.
- Affected docs: `architecture/topology.md`, `architecture/surface.json`,
  `tests/AGENTS.md`.
- Affected code: `topology/transition_provider.h`;
  `tests/tess_topology_movement_test.cc` (two-level fixture, both-direction
  reachability, Builder-only stair, incremental == full across stair edits,
  diagonal skip, same-chunk landing).

## 2026-07-10 - Transition providers for the region graph (M6, S5 slice 7)

- Added: `include/tess/topology/transition_provider.h` -- the
  `TransitionProviderFor<P, World>` concept and the `AdjacentTransitions`
  default. A provider contributes EXTRA directed tile-to-tile transitions
  (stairs, ladders) enumerated once per chunk; `build_region_graph` /
  `update_region_graph` take an optional trailing provider and append one
  directed `RegionPortal` per transition whose endpoints both resolve to
  labeled regions (provider edges are automatically per-class). The landing
  tile must stay within the origin chunk or a face neighbor -- the exact
  range incremental updates re-derive -- asserted in debug builds. The
  provider TYPE is stamped on the graph beside the movement class
  (`matches_provider`); an update with a different provider type falls back
  to a full rebuild. On sparse worlds a transition landing in a non-resident
  chunk marks its origin region as reaching missing topology, so reachability
  degrades to Indeterminate, never a wrong Unreachable.
- Reason: S5 slice 7 -- M6's special-transitions contract. The default
  provider keeps every existing build byte-identical (pinned by test) while
  giving the vertical stair provider (next slice) a sound, incremental-safe
  extension point.
- Affected docs: `architecture/topology.md`, `architecture/surface.json`,
  `tests/AGENTS.md`.
- Affected code: new `topology/transition_provider.h`;
  `topology/topology.h` (provider stamp, per-chunk provider portals,
  reaches-missing pass), `tess.h`, `CMakeLists.txt`;
  `tests/tess_topology_movement_test.cc` (default identity, bridge portals,
  incremental == full, provider-mismatch rebuild, sparse Indeterminate).

## 2026-07-09 - Class-aware agent tick and the runtime class binding (M6, S5 slice 6)

- Changed: the agent pipeline speaks classes end to end.
  `process_unit_path_agents`, `advance_path_agents_with_movement`,
  `tick_unit_path_agents[_with_movement]`, and
  `PathRequestRuntime::process_unit_cached` take `ClassOrTag`;
  `process_weighted_path_agents`, `tick_weighted_path_agents[_with_movement]`,
  and `process_weighted_batch` gain `<World, Class, MaxCost>` forms in which
  ONE movement class drives the search, the precheck, and commit validation,
  while the legacy `<PassableTag, CostTag>` overloads keep the historical
  semantics (LegacyWeighted search; precheck on passability only, matching
  tag-built graphs). `PathRequestRuntime` now binds itself to the movement
  class of each unit process call: the unit route cache keys entries on
  (start, goal) plus a world-version fingerprint and nothing on the class, so
  a rebind clears the unit caches (correct even on misuse) and counts in the
  new `PathRuntimeStats::class_cache_invalidations`; one runtime per
  (world, class) is the PERF contract, not a correctness precondition.
- Reason: S5 slice 6 -- without the binding, a runtime reused across classes
  would serve one class's cached route to another (same start/goal, same
  world version, different passability), the exact cross-class collision the
  graph stamp closes for prechecks. Pinned by test: a Walker-cached 21-step
  detour is never served to a Builder asking the same (start, goal), which
  gets its 7-step route through the wall instead.
- Affected docs: `architecture/path.md`, `architecture/simulation.md`,
  `tests/AGENTS.md`.
- Affected code: `path/path_runtime.h` (class binding, weighted impl split),
  `sim/path_agent.h`, `sim/path_agent_tick.h`;
  `tests/tess_path_movement_class_test.cc` (stale-route regression,
  per-class tick divergence with zero movement rejections).

## 2026-07-09 - Class-aware movement commit validation (M6, S5 slice 5)

- Changed: `validate_movement_intent` / `commit_movement_intent` take a
  movement class OR a raw passable tag (`ClassOrTag`, normalized exactly as
  in astar_path) instead of a bare `PassableTag`. Each endpoint's passability
  is evaluated on its own resolved page -- from and to may live on different
  pages -- replacing the coord-scope `field<PassableTag>` point reads; the
  identity class performs the same resolve+field reads the legacy code did.
- Reason: S5 slice 5 -- plan == commit. A* (slice 2) and the region graph
  (slice 3) already speak the class vocabulary; commit validation was the
  remaining seam still hard-wired to a single global passability, which would
  let a Builder plan a step through a construction site and then have the
  commit reject it. Pinned by test: every step weighted A* accepts for a
  class validates as Moved for that same class, and BlockedFrom/BlockedTo are
  per class on both endpoints.
- Affected docs: `architecture/simulation.md`, `tests/AGENTS.md`.
- Affected code: `sim/movement.h`;
  `tests/tess_path_movement_class_test.cc` (plan==commit property,
  per-class block statuses).

## 2026-07-09 - Precheck class agreement through the graph stamp (M6, S5 slice 4)

- Changed: `precheck_path<ClassOrTag>(graph, world, start, goal, scratch)` --
  the movement class the search uses is now the explicit first template
  argument, and the gate checks `is_region_graph_fresh_for<ClassOrTag>`
  instead of the classless freshness: a graph labeled for a different
  movement class (or predating any stamp) reports `GraphStale` and degrades
  to running A*. `PathRequestRuntime::precheck_prepass<ClassOrTag>` threads
  the class from `process_unit_cached` / `process_weighted_batch` (the
  weighted batch prechecks on PASSABILITY only, matching the legacy weighted
  asymmetry it searches with).
- Reason: S5 slice 4 -- closes the documented precondition hole: the graph
  type encodes only residency, so before the stamp a graph built over a
  different passability compiled fine and its definitive `Unreachable` could
  prune a route the search's own class could walk (the one way the gate could
  turn a solvable query into a wrong failure). That agreement is now enforced
  at runtime, not delegated to the caller.
- Affected docs: `architecture/path.md`, `tests/AGENTS.md`.
- Affected code: `path/precheck.h`, `path/path_runtime.h`,
  `bench/tess_topology_bench.cc` (explicit class argument);
  `tests/tess_path_precheck_test.cc` (wrong-class -> GraphStale, per-class
  rule-out), `tests/tess_path_precheck_runtime_test.cc` (Builder falls back
  to A* under a walker-stamped graph, Builder-stamped graph rules out without
  searching).

## 2026-07-09 - Per-class region labeling and the graph class stamp (M6, S5 slice 3)

- Changed: `build_local_chunk_topology`, `build_region_graph`, and
  `update_region_graph` take a movement class OR a raw passable tag. The
  identity class floods the raw `field_span` exactly as the legacy single-tag
  build did (byte-identical labels and codegen, pinned by test); a composed
  class evaluates its predicate on the resolved page per tile. Portal pairing
  is untouched -- it queries labels, so per-class labels yield per-class
  portals automatically. `RegionGraphT` gains a movement-class stamp
  (`built_class_`, a `core/tag_identity.h` token of the NORMALIZED class)
  recorded at build time and mirrored on `matches_shape`: a stamp mismatch in
  `update_region_graph` forces a full rebuild with the requested class's
  labels, the public `matches_class<ClassOrTag>()` reports the binding, and
  the new `is_region_graph_fresh_for<ClassOrTag>(world, graph)` is the
  class-aware freshness form (later slices route precheck through it so a
  wrong-class graph degrades to GraphStale, never a wrong Unreachable).
- Reason: S5 slice 3 -- the same vocabulary that drives search (slice 2) now
  drives labeling, so a Walker graph and a Builder graph over one world are
  first-class. The stamp closes the documented precheck precondition gap: the
  graph type encodes neither shape nor class, so binding both at build time is
  what keeps a definitive Unreachable trustworthy.
- Affected docs: `architecture/topology.md`, `architecture/surface.json`,
  `tests/AGENTS.md`.
- Affected code: `topology/topology.h`; new
  `tests/tess_topology_movement_test.cc` (identity byte-identity,
  Walker/Builder divergence + Builder-only bridge, per-class incremental ==
  full, stamp-mismatch rebuild, per-class freshness, alloc-free warm
  relabel), `tests/CMakeLists.txt`; `bench/tess_topology_bench.cc` +
  `bench/thresholds/topology.json` (composed-class 512x512 labeling gate;
  measured on par with the raw scan locally, bootstrap threshold 3x the
  identity gate pending CI calibration).

## 2026-07-09 - Movement classes through the A* leaves and weighted cores (M6, S5 slice 2)

- Changed: the pathfinding passability/cost leaves (`detail::is_passable`,
  `is_passable_index`, `tile_entry_cost_index`) now evaluate a movement class
  at the resolved (page, tile) seam. The passability leaves accept a class OR
  a raw tag (normalized through `movement_class_of`, so every legacy
  `<World, Tag>` call site compiles unchanged and stays byte-identical via the
  `WalkableField` identity); the cost leaf requires an actual class, because a
  raw tag would normalize to the unit-cost identity and silently discard a
  legacy `CostTag`. The weighted family -- `weighted_astar_path`,
  `build_weighted_distance_field`, `build_weighted_distance_field_in_box`,
  `build_bounded_weighted_distance_field`, `weighted_distance_field_path`,
  `weighted_path_batch` -- gained single-`Class` cores; the historical
  `<PassableTag, CostTag>` overloads remain as thin forwarders through
  `movement::LegacyWeighted` (exact semantics, including the cost-agnostic
  passability asymmetry). `tag_identity` hoisted from a private
  `FieldProductCache` member to `include/tess/core/tag_identity.h`
  (`tess::detail`) so later slices can stamp graphs and runtimes with a class
  identity. Diagnostics placement is unchanged (`path_cost_read` in the cost
  leaf, `path_passability_check` at callers), so counters do not drift.
- Reason: S5 slice 2 -- one vocabulary must drive search so per-class region
  labeling (slice 3) and precheck/commit agreement (slices 4-5) can share it.
  Fusing the pair into one class also removes the latent trap of mixing a
  passability tag and a cost tag from different logical classes. The
  route-product/portal-route family and the path runtime still carry tag
  pairs and forward through the legacy overloads; they convert with the
  runtime in slice 6.
- Affected docs: `architecture/path.md`, `tests/AGENTS.md`; changelog entries
  from 2026-07-06 moved to `CHANGELOG-archive.md` (token cap).
- Affected code: `path/path.h`, `path/detail/astar.h`,
  `path/detail/weighted_batch.h`, `path/distance_field_box.h`,
  `path/field_product_cache.h`, new `core/tag_identity.h`, `CMakeLists.txt`;
  new `tests/tess_path_movement_class_test.cc` (identity and LegacyWeighted
  node-for-node equivalence, Walker/Builder divergence, sparse missing-chunk
  contract), `tests/CMakeLists.txt`.

## 2026-07-09 - Movement vocabulary DSL (M6, S5 slice 1)

- Added: `include/tess/topology/movement_class.h` (namespace `tess::movement`) --
  a compile-time DSL where a `MovementClass<PassExpr, CostExpr>` fuses a
  passability predicate and an entry-cost expression composed from typed-field
  leaves. Boolean terms `Field`/`NotZero`/`Not`/`AllOf`/`AnyOf`; cost
  expressions `UnitCost`/`ConstantCost`/`FieldCost`/`SelectCost` with a
  `normalize_cost` byte-exact to the weighted A* leaf; identity classes
  `WalkableField`/`WalkableCostField`/`LegacyWeighted`; the `MovementClassFor`
  concept and `movement_class_of` tag/class normalization.
- Reason: first slice of the M6 movement-vocabulary close (S5). Labeling,
  pathfinding, and commit validation currently each bake in a single global
  passability (a raw `PassableTag`); this vocabulary is the shared, allocation-
  free, constexpr foundation later slices thread through region labeling,
  precheck, the weighted agent tick, and `sim/movement.h` so plan and commit
  agree. Leaves read the constexpr `ChunkPage::field<Tag>` at the (page, tile)
  seam because world-scope accessors are not constexpr, and the whole predicate
  inlines to the same ops a hand-written cast emits. `WalkableField` is a
  distinct struct (not an alias) carrying the raw tag + a `field_span` fast path
  so the identity class stays byte-identical to today's single-field scan. Pure
  vocabulary; no wiring yet.
- Affected docs: `architecture/topology.md`, `architecture/surface.json`.
- Affected code: new `topology/movement_class.h`, `tess.h`, `CMakeLists.txt`;
  new `tests/tess_movement_class_test.cc`, `tests/CMakeLists.txt`,
  `tests/AGENTS.md`.

## 2026-07-09 - Path product/cache invalidation contracts (audit-2 W-A)

- Changed: (1) Route, portal-route, and distance-field products treat an empty
  dependency set as invalid, and every builder now captures dependencies for
  non-Found results (`capture_all`), so a cached failure can never replay
  after a world edit that might change the answer. (2)
  `build_distance_field_product` additionally depends on the face neighbors
  of every touched chunk, covering the blocked frontier: an edit that opens a
  fully-sealed chunk now invalidates the product. (3)
  `RouteCacheScratch::store` skips a single route larger than the node cap
  (new `stats().oversized_skips`) instead of invalidating resident entries
  and storing it anyway, and cap value 0 now disables storage rather than
  meaning "unlimited", matching the portal segment cache. (4)
  `build_weighted_portal_route_product` accepts its own `waypoints()` span
  (rebuild-from-own-product no longer reads a cleared vector). (5)
  `PathTicket.generation` widened to 64 bits so retained tickets cannot alias
  across generation wraparound. (6) Documented: the caller-owned staleness
  contract of `cached_astar_path`, the one-cache/runtime-per-world identity
  contract, and the chunk-portal route builder's heuristic (non-authoritative)
  NoPath tier.
- Changed (review follow-up): the multi-agent review of this branch found
  the segment-cache overload of `build_weighted_portal_route_product` had
  been missed (same self-alias UB -- ASan-confirmed -- and missing failure
  capture); the alias guard now covers all product-owned spans (including a
  previously returned `PathResult.path`) via a shared `stash_if_owned`
  helper. It also flagged two quadratic passes and an over-broad capture,
  fixed as: `capture_all` appends directly (O(chunk_count)), the
  blocked-frontier pass dedupes through a scratch seen-set +
  `add_chunk_unique` (linear), and InvalidStart/InvalidGoal products now
  depend only on the offending in-bounds tiles' chunks instead of every
  chunk (out-of-bounds failures carry no dependencies and are permanently
  invalid -- callers pay only the cheap bounds rejection on rebuild).
  `reserve_dependencies` docs now state the chunk_count bound.
- Reason: second audit (2026-07-09) findings H1, H2, M3, M4, M5, M6 and cache
  lows -- failure products carried empty dependency sets that validated
  vacuously forever, and unreached chunks never invalidated field products,
  so removing a wall could leave agents permanently pathless.
- Affected docs: `planning/audit-2026-07-09.md`,
  `planning/audit-2026-07-09-remediation.md`, `decisions/CHANGELOG.md`.
- Affected code: `path/path.h`, `path/portal_route.h`,
  `path/field_product_cache.h`, `path/route_cache.h`, `path/path_runtime.h`,
  `tests/tess_path_test.cc`, `tests/tess_path_cache_test.cc`,
  `tests/tess_path_runtime_test.cc`.
## 2026-07-09 - Executor dispatch guards and partial-plan dirty (audit-2 W-B)

- Changed: (1) `WorkerPoolPhaseExecutor` now documents and enforces its
  single-dispatch contract -- a `dispatch_active_` flag maintained under the
  existing dispatch mutex makes nested or concurrent `for_each_operation`, and
  `reserve_operations` during a dispatch, fail fast via `TESS_ASSERT` in debug
  builds (release builds compile the check out and keep zero overhead). (2)
  `execute_plan` / `execute_plan_deferred_dirty` now include the chunks written
  before an abort in the returned `chunk_count`, and the scheduler tick marks
  pathing dirty whenever any chunk was written, so a plan aborted partway by a
  `PolicyMismatch` no longer leaves path caches stale over already-mutated
  passability. (3) A blocked movement step no longer consumes re-path budget;
  only `prepare_path_agent_processing` counts attempts, so
  `max_blocked_retries = N` grants exactly N re-path attempts (previously a
  movement-blocked cycle was double-counted), and the budget semantics --
  including the by-design indefinite re-path loop against a permanently parked
  blocker -- are documented at `max_blocked_retries`. (4) Thread-spawn loops in
  both executors now join already-started threads and rethrow if a
  `std::thread` constructor throws mid-spawn instead of terminating via a
  joinable-thread unwind; `ScopedThreadPhaseExecutor` documents its no-throw
  callback requirement. (5) `FixedStepAccumulator` clamps the ~1 ulp negative
  bank left by the rounded-division one-tick borrow.
- Reason: second-audit findings H3/M2 (nested/concurrent dispatch deadlocks
  and use-after-realloc were silent), M1 (partially executed plans skipped the
  pathing-dirty mark), and the workstream's low-severity items.
- Affected docs: `decisions/CHANGELOG.md`.
- Affected code: `ops/phase_executor.h`, `ops/queued.h`, `sim/scheduler.h`,
  `sim/path_agent.h`, `sim/path_agent_tick.h`, `sim/time.h`,
  `tests/tess_phase_executor_test.cc`, `tests/tess_sim_scheduler_test.cc`,
  `tests/tess_path_agent_tick_test.cc`, `tests/tess_path_agent_test.cc`,
  `tests/tess_path_runtime_sparse_test.cc`.
## 2026-07-09 - Topology index/shape hardening (audit-2 W-C)

- Changed: (1) `RegionGraph::region_index` guards are wrap-proof -- the chunk
  guard no longer adds 1 before comparing (the out-of-world sentinel ChunkKey
  wrapped past it into an OOB read) and the offset arithmetic is 64-bit (a
  region id near 2^32 wrapped back to a valid-but-wrong index). (2) Region
  graphs now record their build shape (chunk-grid and chunk tile extents);
  `update_region_graph` fully rebuilds and `is_region_graph_fresh` reports
  stale on any mismatch, instead of the chunk-count-only check that let
  equal-count shape mismatches incremental-patch onto wrong adjacency.
  (3) `ShapeTraits` gains division-based (wrap-proof) compile-time overflow
  asserts and an int64 bound on size axes. (4) `detail::box_axis_end` and the
  region-bounds union saturate at int64 max instead of wrapping on extents
  >= 2^63. (5) Documented `ChunkView::is_boundary` degenerate-axis semantics
  and the sparse one-graph-per-world staleness contract.
- Reason: audit findings M7 (region-index wraparound), M8 (shape-mismatch
  incremental patching leaving dangling portal targets), and the related
  topology/shape/meta low-severity items.
- Affected docs: `decisions/CHANGELOG.md`.
- Affected code: `topology/topology.h`, `core/shape.h`,
  `storage/chunk_meta.h`, `block/block.h`, `tests/tess_topology_test.cc`,
  `tests/tess_topology_sparse_test.cc`, `tests/tess_storage_test.cc`.
## 2026-07-09 - Diagnostics alloc-hook and capture-contract fixes (audit-2 W-D)

- Changed: three second-audit fixes on the diagnostics layer. (1) M9: the
  benchmark/test allocation hooks no longer record a deallocation for a null
  pointer -- `operator delete(nullptr)` / `free(nullptr)` are legal no-ops, so
  counting them skewed the deallocations/allocations balance; both the plain
  operator new/delete branch and the sanitizer free hook now null-check before
  recording. (2) M10: the sanitizer-hook branch is excluded on Windows
  (`!defined(_MSC_VER)`, covering MSVC and clang-cl) because MSVC
  /fsanitize=address defines `__SANITIZE_ADDRESS__` but its ASan runtime never
  calls the `__sanitizer_*` hooks and `<pthread.h>` does not exist there.
  (3) M11: documented the threading contract of `capture_timing` /
  `capture_diagnostics` (unsynchronized reads: capture on the recording thread
  or externally synchronize; only the returned snapshot is safe to share) with
  matching notes in trace.h and the ImGui panels header. Docs only, no
  behavior change.
- Reason: findings M9-M11 of the 2026-07-09 second audit.
- Affected docs: `decisions/CHANGELOG.md`.
- Affected code: `bench/tess_diagnostics_alloc_hooks.cc`,
  `diagnostics/export.h`, `diagnostics/trace.h`, `debug/imgui/panels.h`,
  `tests/tess_diagnostics_enabled_test.cc`.
## 2026-07-09 - Test hardening: flake guards, format checking, coverage gaps (audit-2 W-E)

- Changed: test-only hardening from the second audit; no library behavior
  changes. (1) The worker-pool warm no-alloc test tolerates one-time lazy
  runtime allocations on live pool workers by requiring only the last of
  several warm dispatches to be allocation-free (the counter is
  process-global). (2) The ImGui stub's `Text` now carries real ImGui's
  printf-format attribute so `-Wformat` checks panel format strings under the
  `-Werror` presets. (3) The two multi-worker rendezvous spins in the queued
  tests are bounded (30s, clear failure message) instead of hanging to the
  ctest timeout on regression. (4) New coverage: `PrecheckStatus::InvalidStart`
  (out-of-bounds and walled starts), `MovementFailureCounts`
  reserved/stale buckets plus the full `is_transient_movement_failure`
  classification, the four previously unasserted `PathCounters` fields
  (initializations, start/goal passability checks, closed neighbors) via both
  direct events and a real A* maze, a `TESS_ASSERT_MSG` death test, and a
  `UInt128` negative-int constructor death test. (5) Weak assertions
  strengthened: the smoke test pins the released version against a
  hand-maintained literal mirror of the CMake project version instead of
  comparing macros to themselves (a `tess.h` bump that forgets the test now
  fails; a CMake-only bump remains undetectable there), and the warm
  no-alloc agent/tick tests now also pin observable work (submitted/found
  stats, skipped processing with advancement) so a no-op cannot pass. The
  allocation counter documents its relaxed-ordering under-count caveat.
- Reason: second-audit findings M14/M15 plus grep-verified untested surfaces
  and tautological or effect-blind assertions; each weakness could hide a real
  regression (latent flake, unchecked format strings, suite-long hangs,
  silently skipped work).
- Affected docs: `tests/AGENTS.md`.
- Affected code: tests only — `tests/tess_phase_executor_test.cc`,
  `tests/imgui_stub/imgui.h`, `tests/tess_queued_test.cc`,
  `tests/tess_path_precheck_test.cc`, `tests/tess_path_agent_test.cc`,
  `tests/tess_path_agent_tick_test.cc`, `tests/tess_diagnostics_enabled_test.cc`,
  `tests/tess_assert_test.cc`, `tests/tess_shape_test.cc`,
  `tests/tess_smoke.cc`, `tests/allocation_counter.cc`.
## 2026-07-09 - CI and tooling audit remediation (audit-2 W-F)

- Changed: five infrastructure fixes from the 2026-07-09 second audit.
  (1) The `parallel/*` benchmark family is now executed in CI: a
  `tess_bench_parallel_smoke` CTest in the style of the other family smokes,
  and a `parallel/.*` command in `tess_bench_ci_baselines` so baseline
  artifacts accumulate samples. Threshold gating is deliberately deferred:
  repo policy calibrates ceilings from CI baseline artifacts (~3x observed
  maximum), none exist yet for this family, and each gating target enumerates
  an explicit thresholds file, so omitting `bench/thresholds/parallel.json`
  defers the gate without code changes. (2) `tools/benchmark_trends.py`
  discovers every result JSON in a baseline artifact (excluding
  `metadata.json`) instead of reading only block/storage/key, and an
  explicit `--benchmark` selector that matches nothing is now an error
  instead of a silent "n/a"; `tools/benchmark_artifact_metadata.py` gained
  its first tests. (3) `tools/benchmark_thresholds.py` rejects unknown
  per-benchmark limit keys against an allowlist so a typo cannot silently
  disable a gate. (4) A public-safety deny pattern for the private
  downstream consumer's name (split-string style, case-insensitive) so the
  hooks catch reintroduction. (5) Doc drift from the 2026-07-07 gate
  promotions: the Windows MSVC job and public-surface manifest check are
  described as required gates everywhere, the README threshold-suite list
  includes `topology` and `diagnostics`, the advisory GCC compile job is
  listed, stale advisory comments in the workflow were removed or dated,
  and small fixes (path.json note provenance, deck help env vars,
  `.pytest_cache/` ignore, `actions/cache@v4` in the dependency inventory).
- Reason: the audit found compiled-but-never-run benchmarks, tools that
  silently ignored data or typos, a policy without an enforcing pattern,
  and documentation contradicting the actual CI gate status. All fixes are
  infrastructure-only; no tess API or runtime behavior changed.
- Affected docs: `README.md`, `tests/AGENTS.md`, `dependencies.md`,
  `decisions/CHANGELOG.md`.
- Affected code: `bench/CMakeLists.txt`, `bench/thresholds/path.json`,
  `tools/benchmark_trends.py`, `tools/benchmark_artifact_metadata.py`,
  `tools/benchmark_thresholds.py`, `tools/check_public_surface.py`,
  `tools/git_hooks.py`, `tools/steamdeck/deck`, `tests/test_git_hooks.py`,
  `tests/test_benchmark_tools.py`, `.github/workflows/ci.yml`,
  `.gitignore`.

## 2026-07-09 - TraceBuffer pinned to its storage (M12, S4)

- Changed: `diagnostics::TraceBuffer` is now non-copyable and non-movable (all
  four special members deleted).
- Reason: the buffer owns its ring metadata and per-category timing
  accumulators but only references caller storage through a `std::span`. An
  implicit value copy produced two buffers with independent metadata over the
  same backing array, so passing a buffer by value into a helper that installs
  `ScopedTrace` would collect records the caller's original buffer never sees --
  `capture_diagnostics` would silently miss them. Pinning the buffer to its
  storage makes that misuse a compile error; every caller already constructs it
  in place and passes it by reference, so nothing else changed. Flagged by the
  Codex connector review of PR #8.
- Affected docs: `decisions/CHANGELOG.md`.
- Affected code: `diagnostics/trace.h`, `tests/tess_diagnostics_trace_test.cc`.

## 2026-07-09 - Diagnostics review follow-up (M12, S4)

- Changed: three review-feedback fixes on the S4 diagnostics branch. (1) Removed
  the private downstream consumer's name from the new diagnostics docs, using
  generic "downstream consumer/adoption" wording instead -- tracked content is
  treated as public per `AGENTS.md`. (2) Documented the optional, consumer-
  provided Dear ImGui integration dependency that `debug/imgui/panels.h`
  targets. (3) Wired the diagnostics benchmark threshold target into CI so
  `bench/thresholds/diagnostics.json` actually gates regressions.
- Reason: the ImGui-panels and diagnostics slices named the private consumer in
  a repository intended to be public, added an optional dependency without the
  required entry in `docs/dependencies.md`, and shipped a threshold file that no
  CI step exercised (a silent no-gate). All three are addressed here without
  changing any tess API or runtime behavior.
- Affected docs: `architecture/diagnostics.md`, `decisions/CHANGELOG.md`,
  `dependencies.md`.
- Affected code: `.github/workflows/ci.yml`.

## 2026-07-09 - Diagnostics ImGui Panels (M12, S4 slice 5)

- Added (new header `debug/imgui/panels.h`, doubly gated by `TESS_ENABLE_IMGUI`
  && `TESS_ENABLE_DIAGNOSTICS`): reference Dear ImGui panels over the export
  snapshots -- `draw_timing_panel`, `draw_path_counters_panel`,
  `draw_queued_counters_panel`, `draw_allocation_counters_panel`, the composite
  `draw_diagnostics_panel`, and the `category_name` label helper. tess core
  never fetches or links ImGui; the consumer defines the gates on its own
  target and includes `<imgui.h>` before the header (a `#error` enforces the
  order). Only Text/TextUnformatted/Separator are used, with portable
  `unsigned long long` printf casts. Not included by `tess.h`.
- Reason: fifth slice of the M12 diagnostics close (S4) -- the ImGui skeleton.
  The panels are the reference renderer a downstream overlay adopts in the final
  slice. tess validates the header against a minimal ImGui stub
  (`tests/imgui_stub/imgui.h`, `tess_diagnostics_panels_test`) so a panel bug is
  caught in tess CI, not only in the consumer; the stub mirrors the real API for
  the three primitives used.
- Affected docs: `architecture/diagnostics.md`, `architecture/surface.json`.
- Affected code: new `debug/imgui/panels.h`, `tests/imgui_stub/imgui.h`,
  `tests/tess_diagnostics_panels_test.cc`, `CMakeLists.txt`,
  `tests/CMakeLists.txt`.

## 2026-07-09 - Diagnostics Benchmark Family (M12, S4 slice 4)

- Added: `bench/tess_diagnostics_bench.cc` and `bench/thresholds/diagnostics.json`,
  a gated `diagnostics/` benchmark family compiled only into the
  diagnostics-enabled `tess_bench_diagnostics` binary (the source is an empty TU
  without `TESS_ENABLE_DIAGNOSTICS`). Benches: `diagnostics/trace_record`,
  `diagnostics/record_timing`, `diagnostics/scoped_timer`,
  `diagnostics/warning_sink`. New `tess_bench_diagnostics_thresholds` target, a
  CI baseline command, and a `tess_bench_diagnostics_family_smoke` CTest.
- Reason: fourth slice of the M12 diagnostics close (S4). Measures the enabled
  overhead of the trace/timer/warning primitives directly (local observations
  ~0.3-32 ns, far under the loose bootstrap ceilings). The compile-down side is
  proven structurally: the default `tess_bench` compiles every diagnostic macro
  to nothing, so its `queued/`/`path/` families are the zero-overhead baseline
  and still pass unchanged with the planner-trace macros present.
- Affected docs: `planning/benchmark-plan.md`.
- Affected code: new `bench/tess_diagnostics_bench.cc`,
  `bench/thresholds/diagnostics.json`, `bench/CMakeLists.txt`.

## 2026-07-09 - Planner Trace and Snapshot Export (M12, S4 slice 3)

- Added (ops/queued.h): planner-trace instrumentation. `plan_operations` (now
  an index loop) records `invalid_write_policy` / `invalid_field_access` /
  `invalid_domain` / `conflict` / `planned` per operation, and
  `plan_parallel_execution_phases` records `unsupported_write_policy` /
  `new_phase` / `merged` per operation, all under the `Planner` trace category
  via `TESS_DIAG_TRACE_VALUE` (value = operation/phase index). Macros only:
  byte-identical planning when diagnostics are off.
- Added (diagnostics, new header `diagnostics/export.h`, gated by
  `TESS_ENABLE_DIAGNOSTICS`): `TimingSnapshot` (a copy of every category's
  `TraceCategoryStats` with a Count-guarding `category()` accessor),
  `DiagnosticsSnapshot` (path/allocation/queued counters + timing), and the
  `capture_timing` / `capture_diagnostics` free functions that assemble them as
  pure copies decoupled from the live sinks.
- Reason: third slice of the M12 diagnostics close (S4). The planner was the one
  major queued-ops surface with zero instrumentation; routing its decisions
  through the trace buffer gives the profiling panels a real event log, and the
  export snapshots are the plain structs the ImGui panels and the downstream
  adoption render. The `plan_operations` loop switched to an index form purely
  to expose the operation index as the trace value; behavior is unchanged (the
  full 514-test suite passes).
- Affected docs: `architecture/diagnostics.md`, `architecture/surface.json`.
- Affected code: `ops/queued.h`, new `diagnostics/export.h`, `tess.h`,
  `CMakeLists.txt`; extended test `tess_diagnostics_trace_test.cc`.

## 2026-07-09 - Diagnostics Trace Buffer and Timers (M12, S4 slice 2)

- Added (diagnostics, new header `diagnostics/trace.h`, gated by
  `TESS_ENABLE_DIAGNOSTICS`): a structured event log and timing capture.
  `TraceCategory` (coarse origin tag with a `Count` sizing sentinel),
  `TraceRecord` (category + non-owning `label` + `value` + monotonic
  `sequence`), `TraceCategoryStats` (samples/total/min/max ns), and
  `TraceBuffer` -- a caller-owned ring over a `std::span<TraceRecord>` with an
  inline per-category timing accumulator (allocation-free; empty span drops all
  records; overflow overwrites oldest and counts `dropped()`). `ScopedTrace`
  installs the thread's active buffer; `trace_event` and new
  `TESS_DIAG_TRACE` / `TESS_DIAG_TRACE_VALUE` macros route to it (and compile to
  nothing when off). `ScopedTimer` is a `steady_clock` RAII timer that binds to
  the buffer active at construction and, on destruction, folds the elapsed
  nanoseconds into the category accumulator and appends a duration record.
- Reason: second slice of the M12 diagnostics close (S4). Trace records and
  timers are the profiling substrate the ImGui panels and the downstream adoption
  render, and the planner-trace slice records against them. `ScopedTimer`
  captures the active buffer at construction (not destruction) so nested scopes
  and destruction order cannot misattribute a timed span. The `label`
  non-owning contract mirrors `Warning::message`/`PathView`.
- Affected docs: `architecture/diagnostics.md`, `architecture/surface.json`.
- Affected code: new `diagnostics/trace.h`, `diagnostics/diagnostics.h` (the two
  new trace macros), `tess.h`, `CMakeLists.txt`; extended tests
  `tess_diagnostics_trace_test.cc` and `tess_diagnostics_default_test.cc`.

## 2026-07-09 - Diagnostics Warning Sink (M12, S4 slice 1)

- Added (diagnostics, new header `diagnostics/warning_sink.h`, gated by
  `TESS_ENABLE_DIAGNOSTICS`): a `Warning` record (a `WarningCategory` origin
  tag, a non-owning `std::string_view message`, a numeric `detail`, and a
  defaulting `std::source_location`), the `WarningSink` concept
  (`noexcept warn(const Warning&)`), `NullWarningSink` (discards), and
  `BufferedWarningSink<Capacity>` -- a caller-owned fixed-capacity ring with
  inline storage (allocation-free `warn()`), oldest-first indexing, and a
  `dropped()` overflow count.
- Reason: first slice of the M12 diagnostics close (S4). A warning channel is
  a foundational primitive that later stages consume (queued-ops result
  reasons in S6, scheduler budget signals in S7); landing it early and
  independently keeps the trace/timer slice focused. The `message` non-owning
  contract mirrors `PathView`: warnings are copied by value but never own their
  text, so the sink stays allocation-free.
- Affected docs: `architecture/diagnostics.md`, `architecture/surface.json`.
- Affected code: new `diagnostics/warning_sink.h`, `tess.h`, `CMakeLists.txt`;
  new test `tess_diagnostics_trace_test.cc`.

## 2026-07-09 - PathView Non-Owning Path View (M8)

- Added (path, new header `path/path_view.h`): `PathView`, a non-owning view
  over a path's coordinates. `PathResult::path` changes type from
  `std::span<const Coord3>` to `PathView` (an intentional but source-compatible
  break -- see below). PathView offers read-only span parity (`size`, `empty`,
  `operator[]`, `front`, `back`, `begin`/`end`, `data`), `span()` to recover the
  raw `std::span`, and `suffix(offset)`: the bounds-clamped remaining path from a
  walked index, sharing the same storage without copying. It carries the same
  lifetime contract as the span it wraps (valid until the A* scratch or runtime
  node buffer it views is reused).
- Reason: closes the M8 `PathView` deliverable and gives consumers a named path
  type plus the one operation real consumers need -- "the remaining path from
  index N" (an agent advancing along `path_index`, an overlay drawing the rest
  of a route). PathView is constructible from a `std::span` or a
  `std::vector<Coord3>`, so every existing result-construction site
  (`PathResult{..., scratch.path_}`) compiles unchanged, and its span-parity API
  means the ~250 span-style `.path` reads across the codebase compile unchanged;
  the only edited call site extracts a raw span via `.path.span()`. Scoped to
  `PathResult`; `DistanceFieldResult` (no path) and the route-product/waypoint
  spans are deliberately left as plain spans.
- Affected docs: `architecture/path.md`, `architecture/surface.json`.
- Affected code: new `path/path_view.h`, `path/path.h`,
  `path/portal_segment_cache.h`, `tess.h`, `CMakeLists.txt`; new test
  `tess_path_view_test.cc`.

## 2026-07-09 - Topology Benchmark Family

- Added: `bench/tess_topology_bench.cc` and `bench/thresholds/topology.json`, a
  new gated `topology/` benchmark family covering region-graph build over an
  open 512x512 world, single-chunk incremental update, a far reachability query,
  and `precheck_path` on both a reachable and a sealed (enclosed-goal)
  destination. Wired into `tess_bench` / `tess_bench_diagnostics`, the
  `tess_bench_topology_thresholds` gate target, the non-gating CI baseline
  collection, a smoke test, and a new CI "Topology benchmark thresholds" step.
- Headline: `topology/precheck_unreachable_512x512` floods only the region graph
  (~256 regions) to prove a goal unreachable, versus `path/astar_plane_gap_miss_512x512`
  which floods the full tile component (~262k tiles) for the same NoPath verdict
  -- the cost the precheck short-circuits.
- Reason: the topology precheck (S3) needs a regression guard, and M14 tracks a
  topology bench family. Per the benchmark policy, initial `max_cpu_time_ns`
  ceilings are deliberately generous documentation bounds (not tuned gates) to
  avoid false failures on slower/noisier CI runners; they are tightened once in
  the S11 consolidation from accumulated CI baselines.
- Affected code: `bench/tess_topology_bench.cc`, `bench/thresholds/topology.json`,
  `bench/CMakeLists.txt`, `.github/workflows/ci.yml`.

## 2026-07-09 - Topology Precheck Wired Into the Path Runtime

- Added (path runtime / sim): `PathRequestRuntime::process_unit_cached` and
  `process_weighted_batch`, the `process_*_path_agents` helpers, and all four
  `tick_*_path_agents` entry points now take an optional trailing
  `const RegionGraphT<World::residency_type>*` (default `nullptr`). When a graph
  is supplied, a pre-A* pass resolves every request `precheck_path` proves
  `Unreachable` to `NoPath` (zero expanded nodes) without searching, counting
  them in the new `PathRuntimeStats::precheck_ruled_out` /
  `PathAgentFrameStats::precheck_ruled_out` -- a SUBSET of `no_path`, so
  aggregate failure counts are unchanged. The unit path skips ruled-out requests
  in its search loop; the weighted path runs the monolithic `weighted_path_batch`
  over only the survivors and scatters results back to their original slots.
- Reason: closes the runtime half of the M8 precheck gap. `nullptr` (the
  default) is byte-identical to the previous behavior, so this is opt-in and
  additive. Precondition: the graph must be built over the same `PassableTag`
  the search uses; freshness is checked (`is_region_graph_fresh`) and a stale
  graph degrades to running A*, so the gate can only prune provably-unreachable
  goals, never turn a solvable query into a wrong failure.
- Affected docs: `architecture/path.md`, `architecture/simulation.md`.
- Affected code: `path/path_runtime.h`, `sim/path_agent.h`,
  `sim/path_agent_tick.h`; new test `tess_path_precheck_runtime_test.cc`.
  The downstream adoption (passing its `RegionGraph` to the weighted tick and
  deleting its bespoke reachability gating) lands in a later S3 slice.

## Earlier Entries

Design-changelog entries before 2026-07-09 are archived in
[`CHANGELOG-archive.md`](CHANGELOG-archive.md) to keep this file under the
24k-token per-file limit. New entries go at the top of this file; when it
approaches the limit again, its oldest entries move to the archive.
