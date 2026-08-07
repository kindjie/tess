# Budgeted-progress benchmarks

Status: **Proposed design; no implementation**  
Audited against `main` at `bd304e73063bbb17561180f578d6d81ca2979096`.

## 1. Goal and constraints

This suite answers:

> **How much useful grid/navigation work can Tess complete within a game-representative time budget on given hardware?**

It complements, rather than replaces, the existing latency microbenchmarks. The independent variable is a host-granted Tess wall-clock budget; the primary outputs are useful completions, deadline success, cooperative budget overshoot, outstanding-work stability, and sustainable arrival rate/population.

The current code constrains the design:

- `FixedStepAccumulator` converts rendered-frame time into deterministic fixed simulation ticks; `run_schedule_frame` executes one schedule tick per grant (`include/tess/sim/time.h`, `include/tess/sim/schedule.h`).
- `Schedule` intentionally budgets background work in deterministic **items**, not wall time.
- `ResumableWorkQueue` is Tess's existing cooperative continuation model; one continuation callback remains indivisible (`include/tess/ops/async_work.h`).
- Core A*, weighted search, distance/field-product construction, `PathRequestRuntime::process_*`, and region-graph build/update are synchronous today (`docs/architecture/path.md`, `docs/architecture/topology.md`).
- Tess already has `FlowAccounting`, deterministic map/colony harnesses, a declarative workload matrix, benchmark artifact metadata, and controlled-hardware campaign tooling.

The benchmark must therefore measure **granularity as well as throughput**. It must not add a production wall-clock scheduler, imply hard preemption, or invent benchmark-only partial results.

## 2. Reuse existing infrastructure

| Existing facility | Reuse |
| --- | --- |
| `FixedStepAccumulator`, `SimClock`, `SimTimeControl` | Frame-to-tick model |
| `Schedule`, `BackgroundBudget`, `ResumableWorkTask` | Existing task/tick semantics |
| `FlowAccounting` / `FlowCounters` | Admission, terminal, outstanding, age, work-unit accounting |
| `tests/colony_harness.h` | Deterministic colony world, churn, movement/path/topology stack |
| `tests/grid_benchmark_harness.h`, `grid_map_generators.h` | Deterministic maps/endpoints and independent path oracle |
| `bench/workload-matrix.json` | Canonical workload vocabulary |
| Existing path/field/topology result counters | Algorithmic work units |
| `benchmark_artifact_metadata.py` + campaign tooling | Machine/toolchain metadata and controlled runs |

Budgeted scenarios add budget/TPS/demand/deadline dimensions but should reference existing workload/scenario identities rather than define a second taxonomy.

## 3. Exact frame and tick model

### 3.1 Frame budget

Canonical render rate: **60 FPS**, conceptual period `1/60 s = 16.666666... ms`. Feed `1.0 / 60.0` to the existing accumulator and keep `max_ticks_per_frame = 8` unless a scenario explicitly overrides it.

Canonical Tess budgets are exact integer nanoseconds:

```text
{125000, 250000, 500000, 1000000, 2000000, 4000000, 8000000}
```

or `{0.125, 0.25, 0.5, 1, 2, 4, 8} ms`.

Canonical `base_tps` campaign values are `{20, 30, 60, 120}`; arbitrary supported positive rates remain configurable. `Speed1x` is canonical and speed is always recorded.

### 3.2 Per-frame versus per-tick budgets

The headline mode is:

```text
budget_scope = frame
```

One rendered frame gets **one** allowance `B_frame`, shared by every simulation tick granted during that frame. If 120 TPS yields two ticks in a 60 FPS frame, the total entitlement is still `B_frame`, not `2 * B_frame`.

A secondary mode may use:

```text
budget_scope = tick
```

where each granted simulation tick gets `B_tick`. This can consume approximately `N * B_tick` in a frame with `N` ticks and therefore answers a different question. Never merge per-tick results into the adopter-facing frame-budget curve.

### 3.3 Mandatory work

Current tick-coupled work that cannot safely defer runs when due even after the nominal frame allowance is exhausted; the excess is recorded as overshoot. Defer-capable work may remain outstanding.

`BudgetPolicy` is intent metadata, not evidence that a consumer is currently resumable or wall-clock-budgeted. Capability comes from the actual API boundary.

## 4. Honest cooperative enforcement

The wall-time controller belongs in the benchmark driver, not production `Schedule`.

```text
frame_start = clock.now()
deadline = frame_start + budget

while eligible defer-capable work exists:
    if clock.now() >= deadline:
        break
    execute one existing correctness-preserving quantum to normal return
    account its result

frame_end = clock.now()
overshoot = max(0, frame_end - deadline)
```

A quantum started while time remained always finishes. No callback, A* loop, field build, topology transaction, or parallel phase is interrupted mid-call unless an existing production API already exposes a continuation boundary.

For `ResumableWorkQueue`, the driver may check wall time between `advance()` calls; the finest canonical characterization uses `AsyncWorkBudget{1}` where one item is a valid atomic unit. A single callback can still overshoot.

Do not use predicted operation duration to avoid starting a quantum in v1: overshoot caused by coarse interruption granularity is a result, not noise to hide.

### 4.1 Deterministic service order

This is benchmark policy, not a new production scheduler contract.

- Current-fidelity mixed ticks preserve existing `SimPhase`, task ordering, and dependency edges.
- Benchmark-owned defer-capable queues select only dependency-ready items, then existing `Priority` order (`Immediate` to `Maintenance`), then earliest inclusive simulation deadline, then admission sequence.
- `CanSkipIfSuperseded` applies only after an explicit scenario supersede/coalesce transition; it is not permission to drop arbitrary work.

Record a versioned service-policy identifier in every trace/artifact.

## 5. Operation capability matrix

"Useful completion" is a correctness-valid result a caller can consume. Internal algorithmic progress is reported separately.

| Operation | Useful completion | Work units | Current quantum | Resumable now? | v1 slicing |
| --- | --- | --- | --- | --- | --- |
| Unit / weighted A* | terminal `PathResult` | expanded/reached nodes; path nodes secondary | whole query | No | between independent queries only |
| Reachability / coarse path | terminal query result | visited regions / reconstructed regions | whole query | No | between queries |
| Distance-field build | complete valid field | reached nodes; queue/bucket work if exposed | whole build | No | between builds |
| Field-product build | complete stamped product | reached nodes, goals, dependencies | whole build | No | between products |
| Product replay / nearest target | terminal result/path | reconstructed nodes where exposed | whole query | No | between queries |
| `PathRequestRuntime::process_unit_cached` | correct batch result set | requests, expanded nodes, cache/group/build stats | whole process call | No | do not split a batch under test |
| Weighted runtime/batch planner | correct batch result set | requests, goals, field builds, fallbacks, copied nodes | whole process call | No | same restriction |
| Path-agent tick driver | applied routes + valid movement results | submitted/completed, advances, arrivals, retries | whole tick driver | No | observe budget before/after only in fidelity mode |
| Local chunk topology build | valid `LocalChunkTopology` | passable tiles, regions, exits | one chunk build | No internally | between independent chunk builds |
| Region-graph build/update | one fresh consistent graph | dirty/affected chunks, regions, portals | whole transaction | No | no partial graph publication |
| One queued op per chunk | terminal op/ack after dirty merge | operations, chunks, tiles, dirty records | existing op/phase boundaries | Not a continuation | may slice only where lower-level APIs preserve semantics |
| `AutoExecTask` | complete planned run/result drain | ops/chunks/phases | whole task | No | indivisible in fidelity mode |
| `ResumableWorkQueue` | ticket reaches terminal/`Ready` | existing `items_done`, offered/consumed units | one callback / `advance()` | **Yes** | yes; reference continuation model |

If small-budget results are dominated by an indivisible operation, separately justify a production continuation API for that operation. Likely candidates are single-query pathfinding, field/product construction, runtime batches preserving grouping/cache behavior, region-graph transactions, and possibly a persistent queued-plan cursor. Do not create these merely to make the benchmark easier.

Partitioning one 100-request batch into 100 independent calls is **not** evidence that the batch is resumable if it changes grouping/cache behavior.

## 6. Experiment A: isolated capacity

Question: with a permanently available supply of one operation, how many correct results fit in each budget, and what quantum causes overshoot?

### 6.1 Canonical cells

Reuse existing workload builders/catalog cells for:

1. unit point path, 512x512 / 32x32 chunks, structured room/portal-style layout;
2. weighted point path, 512x512 / 32x32, weighted structured/sparse-blocker layout;
3. existing repeated/shared-goal 100-request batch, kept as one current batch call;
4. field-product build, 512x512, eight deterministic goals;
5. colony-derived incremental topology update with four deterministic dirty chunks;
6. queued one-op-per-chunk update path including planning/execution/dirty merge;
7. a real `ResumableWorkQueue` workload to validate actual continuation behavior.

Generate a large frozen request pool before timing using the existing deterministic generator/oracle machinery and seed `0x5C0107`. For point queries keep at least 10,000 requests available. No random generation, parsing, allocation setup, or oracle work occurs inside the Tess service timer.

### 6.2 Modes

**Saturated:** eligible inventory never empties. Report useful completions/frame and /second, algorithmic work/second, and overshoot distribution.

**Arrival-rate:** release offers from a deterministic rational-rate accumulator (integer/Bresenham-style, not random sampling). Geometrically bracket and then refine the highest flow-stable rate. Save every tested point and trace hash.

## 7. Experiment B: realistic mixed colony

Reuse the colony harness setup and dependencies but drive it from a real 60 Hz render loop.

### 7.1 Reference scenario

```text
logical map        64x64 room-and-corridor
seed               0x5C0107
world              512x512x1
chunk              32x32x1
schema             existing narrow colony schema
movement cost      existing deterministic 1..4 weighted cost
agents             100
goal distance      24 tiles
churn              every 8 simulation ticks
churn magnitude    4 deterministic chunks/event
render             60 FPS
TPS                20 reference; then 30, 60, 120
max ticks/frame    8
executor           serial reference
budgets            all seven canonical frame budgets
```

This anchors directly to `tess_colony_harness_test.cc`'s existing 512x512 / 100-agent / churn-every-8 / four-chunk base configuration.

Capacity cell:

```text
world              1024x1024x1
chunk              32x32x1
population ladder  100, 250, 500, 1000; 2000 only if placement succeeds
```

Reject any point with `agents_unplaced != 0`; never silently benchmark a smaller population. A later controlled campaign may add the redesign's 2048x2048/up-to-10k tier.

### 7.2 Stationary demand

The existing test becomes idle as agents arrive. For capacity measurement, give each agent a deterministic endpoint pair and re-arm the opposite endpoint on the tick after arrival. This produces repeatable ping-pong navigation demand while reusing the existing world, movement, path, churn, topology, and flow-accounting machinery.

Canonical end-to-end navigation deadline:

```text
arrival_deadline = admission_tick + 32 simulation ticks
```

The nominal path distance is 24 tiles, leaving eight ticks of scenario slack. This is a benchmark SLO, not a Tess API guarantee.

### 7.3 Two views

**`mixed_current_fidelity`** preserves current production boundaries. Whole `AutoExecTask`, topology, runtime batch, or path-agent calls may overshoot. This answers "what happens today?"

**`mixed_existing_quanta`** may defer only at public boundaries already safe to defer/resume; it never turns a synchronous path batch or topology transaction into a new algorithm. This answers "what can a host do today using Tess's existing cooperative quanta?"

If the views are identical because relevant work is not resumable, that is a valid result.

## 8. Deterministic demand traces and flow accounting

Pre-generate a versioned trace. Each event contains at least:

```text
trace_event_seq
simulation_tick
demand_class
workload/scenario reference
payload/request index
coalesce_key (optional)
admission-policy/capacity reference
deadline_tick (inclusive)
```

Record generator version, parameters, seed, and SHA-256 of the materialized logical trace. Equal-tick events execute in `trace_event_seq` order. Wall-clock observations never generate demand.

### 8.1 Use Tess lifecycle semantics

Use the existing `FlowCounters` terms:

- offer outcomes: `admitted`, `rejected`, `coalesced_into_pending`;
- terminal outcomes: `completed`, `cancelled`, `superseded`, `stale`, `failed`, `dropped_after_admission`;
- inventory/age: `outstanding_current`, high-water, tick-weighted inventory, residence, oldest age;
- work units: offered/consumed.

Use production-attached `FlowAccounting` directly where available (`PathAgentTickState`, `ResumableWorkQueue`, event/maintenance flows). Isolated adapters without a built-in flow owner may account transitions at the public operation boundary using the same `FlowAccounting`; do not define competing conservation semantics.

Hard invariants:

```text
offered = admitted + rejected + coalesced_into_pending
admitted = terminal + outstanding_current
```

Any cell violating either identity is invalid regardless of timing quality.

Do not add a separate source-of-truth backlog counter. Derived report labels are:

```text
backlog_current    = outstanding_current
backlog_high_water = outstanding_high_water
backlog_growth     = delta(outstanding_current) / observation_duration
oldest_age         = oldest_outstanding_age_ticks
```

### 8.2 Deadlines

Deadlines are inclusive **simulation ticks**, not rendered frames or wall time. Successful completion at tick `D` meets deadline `D`; tick `D+1` does not. Failure/stale/cancel/supersede is terminal for conservation but not successful deadline completion.

Recommended isolated defaults:

| Demand | Deadline |
| --- | ---: |
| interactive point path | admission + 1 tick |
| queued terrain update | admission + 1 tick unless current-tick mandatory |
| required topology refresh | same tick, otherwise explicitly +1 |
| shared field/product refresh | admission + 4 ticks |
| background continuation | admission + 20 ticks |

These are scenario definitions, not public promises.

## 9. Metrics and capacity definition

Every cell emits:

- useful completions total, per frame, per wall second, per simulation second;
- operation-specific algorithmic work units;
- deadline success rate and lateness p50/p95/p99/max;
- per-frame Tess elapsed time and overshoot p50/p95/p99/max plus overshoot-frame rate;
- `outstanding_current` start/end, high-water, growth rate, tick-weighted inventory;
- oldest outstanding age p50/p95/p99/max;
- terminal outcome counts, admission/rejection/coalescing counts;
- starvation/fairness metrics;
- sustainable arrival rate or population.

A partial A* or partially rebuilt graph never counts as useful completion unless a production API exposes a valid partial result.

### 9.1 Starvation/fairness

An item is reportably **starved** if it remains continuously eligible but receives no service quantum for:

```text
max(4 * its deadline allowance, effective_tps)
```

— four deadline windows or one simulation second, whichever is longer. Report count/max no-service age by priority/class.

For homogeneous multi-producer fairness tests, report Jain's index over producer-normalized useful-completion rates. Do not combine unlike priority classes into one fairness number; mixed workloads report per-class service share and oldest age.

### 9.2 Flow-stable capacity

Take `FlowCounters` snapshots at the start/end of the measured window. With `A` measured-window admissions, a point is **flow-stable** when:

1. both conservation identities hold;
2. positive outstanding growth is at most `max(1, ceil(0.005 * A))` items (0.5% plus one-item integer slack);
3. final oldest outstanding age does not exceed the scenario starvation window;
4. the scenario deadline target is met.

Canonical deadline target: **99%** successful completion by deadline. Raw data must allow 95%/99.9% summaries when sample counts support them.

Do not bake a universal overshoot tolerance into "sustainable" in v1. A load can be flow-stable while producing unacceptable frame spikes. Put p99/max overshoot beside every maximum flow-stable arrival rate/population. A future product policy may define a separate "frame-safe" threshold.

A quiescent drain may be used for correctness equivalence but never to erase measured-window outstanding growth.

## 10. Sliced versus contiguous equivalence

For every operation claimed sliceable, freeze world/input/request order and run:

- **contiguous:** same production algorithm to terminal completion with no wall-budget stop;
- **sliced:** same algorithm/input, yielding only at its documented existing quantum.

After drain, compare the strongest stable result available:

- exact path status/cost/path;
- field/product status, stamps, and deterministic content hash;
- topology freshness plus labels/portal/index hash and fixed reachability probes;
- queued final world fields, dirty/version state, and acks;
- mixed final world/agent state and scenario counters where semantic equivalence is claimed.

Changing world versions while work is pending requires identical invalidation checkpoints in both runs.

Forbidden shortcuts: splitting a batch into a different batching strategy, creating a benchmark-only tile/chunk field algorithm, publishing partial topology, reordering requests for locality, or freezing updates only in sliced mode. If equivalence needs a new continuation API, mark the current operation non-resumable.

## 11. Timing methodology

### 11.1 Clock and observer cost

Use a monotonic clock (`steady_clock` unless a controlled platform selects another). Record clock identity/resolution, clock-read median/p95 cost, and empty controller-loop cost. This matters at 0.125 ms.

Pre-generate trace data outside the Tess service timer. The primary service timer includes budget-controller checks, Tess calls, and necessary deterministic service-selection bookkeeping; it excludes trace generation/parsing, oracle work, serialization, and summary/histogram construction.

### 11.2 Timing versus instrumentation

Follow existing campaign practice:

1. **timing pass:** minimum instrumentation; only cheap boundary-returned counters;
2. **counter pass:** identical trace hash, detailed deterministic counters;
3. optional **PMU pass:** controlled hardware only, never the source of published wall time.

Do not publish timing gathered under diagnostics allocation hooks or `perf` wrapping.

### 11.3 Parallel counter aggregation

Current diagnostics are thread-local; pool-worker counters do not automatically aggregate into the caller sink. Never publish incomplete parallel counts.

Use frame-owner counters already returned after join, or benchmark-owned per-worker/per-operation slots reduced deterministically after join/outside timing. A separate serial instrumentation pass is acceptable only for a counter proven invariant to executor width. Do not add contended hot-loop atomics solely for measurement.

### 11.4 Warmup, repetitions, percentiles

Warm-throughput cell:

```text
fresh scenario per repetition
120 unmeasured frames
600 measured frames
10 repetitions
```

Capacity-boundary confirmation:

```text
300 warmup frames
1800 measured frames
5 repetitions
```

Cold-start is separate: no warmup, first 120 frames retained, 20 fresh repetitions.

Minimum sample counts before publishing a percentile:

```text
p50: 20
p95: 200
p99: 2000
```

Otherwise emit `insufficient_samples`. Preserve repetition boundaries; pooled frame percentiles are descriptive, not a substitute for per-repetition variability.

Deterministically rotate budget order by repetition instead of always running 0.125 -> 8 ms, reducing correlation with thermal drift while retaining reproducibility.

### 11.5 Controlled-hardware metadata

Extend/reuse current artifact and campaign metadata. Publish at least commit/dirty identity, compiler/flags, OS/kernel, CPU model, socket/core/thread topology, memory, governor/power and boost state where controllable, process/worker affinity, NUMA policy, executor/worker count, clock identity/resolution, thermal/power observations available from platform tooling, and all frame/TPS/budget/scenario versions.

Keep existing public sanitation: no hostnames, checkout paths, or irrelevant identifying machine data.

## 12. Versioned artifacts and summaries

Use a suite-specific schema rather than forcing a multi-frame queueing experiment into one Google Benchmark row:

```json
{
  "schema": "tess.budgeted_progress.v1",
  "suite_version": 1,
  "run": {
    "commit": "...",
    "machine_fingerprint": "...",
    "compiler": "...",
    "bench_flags": "..."
  },
  "experiment": {
    "kind": "mixed_current_fidelity",
    "scenario_id": "colony-roomcorridor-v1",
    "workload_refs": ["scenario/colony", "path/agent", "topology/incremental"],
    "seed": 6029575,
    "frame_hz_num": 60,
    "frame_hz_den": 1,
    "sim_tps": 20,
    "sim_speed": "1x",
    "max_ticks_per_frame": 8,
    "budget_scope": "frame",
    "budget_ns": 1000000,
    "executor": {"kind": "serial", "workers": 1}
  },
  "trace": {"version": 1, "sha256": "..."},
  "flow": {
    "offered": 0,
    "admitted": 0,
    "rejected": 0,
    "coalesced_into_pending": 0,
    "completed": 0,
    "cancelled": 0,
    "superseded": 0,
    "stale": 0,
    "failed": 0,
    "dropped_after_admission": 0,
    "offered_work_units": 0,
    "consumed_work_units": 0,
    "outstanding_current": 0,
    "outstanding_high_water": 0,
    "inventory_tick_weighted": 0,
    "residence_ticks_accumulated": 0,
    "oldest_outstanding_age_ticks": 0,
    "admission_identity_ok": true,
    "retention_identity_ok": true
  },
  "summary": {
    "measured_frames": 600,
    "useful_completions": 0,
    "deadline_success_rate": 0.0,
    "budget_overshoot_frames": 0,
    "budget_overshoot_ns_p99": 0,
    "backlog_growth_per_sim_second": 0.0,
    "oldest_age_ticks_p99": 0,
    "starved_items": 0,
    "fairness_jain": null,
    "flow_stable": false,
    "correctness_hash": "..."
  }
}
```

Additive fields may remain v1; semantic changes require v2. Raw frame/request samples may live in compressed sidecars keyed by the same run/cell/trace identity.

Generate concise CSV/Markdown capacity rows:

```text
scenario | sim_tps | budget_ms | useful/s | deadline_success |
max_flow_stable_rate_or_population | backlog_drift/s |
p99_overshoot_us | max_oldest_age_ticks
```

Isolated headline: useful completions/frame or /second versus budget. Mixed headline: maximum flow-stable tested population versus budget, always beside world size, TPS, executor, machine fingerprint, deadline success, and p99 overshoot.

## 13. Deterministic fake-clock tests

Make the benchmark runner depend on a small clock interface/concept. Real campaigns use a monotonic clock; tests use an integer-nanosecond scripted clock whose fake quantum advances time deterministically—no sleeps.

Required boundaries:

1. zero budget starts no defer-capable work;
2. positive budget starts the first eligible quantum;
3. exact-deadline completion has zero overshoot and starts no next quantum;
4. one-nanosecond overrun records exactly one nanosecond;
5. a long indivisible first quantum completes and counts useful work plus overshoot;
6. resumable items stop only between `advance()` calls and resume without duplicates;
7. multiple ticks in one rendered frame share one frame allowance;
8. per-tick mode intentionally resets allowance each tick;
9. 0/1/2+-tick accumulator patterns release demand/deadlines correctly;
10. completion exactly at simulation deadline succeeds; next tick misses;
11. admission/coalesce/reject transitions preserve the admission identity after each event;
12. all terminal transitions preserve retention identity;
13. oldest outstanding age tracks the earliest pending admission;
14. starvation time counts only while dependency-ready;
15. quiescent drain cannot retroactively change the measured stability verdict;
16. large nanosecond values do not underflow elapsed/overshoot arithmetic.

These tests belong in normal deterministic CI even though hardware timing curves do not.

## 14. Packaging and campaign policy

### 14.1 Separate binary

Recommendation: add a dedicated executable:

```text
tess_bench_budgeted_progress
```

Do not register the seven-budget x TPS x scenario campaign in `tess_bench`. Tess already isolates `tess_bench_thread_scaling` because unfiltered main-suite call sites would otherwise run an expensive sweep and negative filters fail open. Budgeted progress has the same issue plus its own clock/controller, trace replay, capacity search, and schema.

Share workload support; separate execution, not definitions.

### 14.2 Controlled campaign artifacts first

Initial real-time results should be **controlled campaign artifacts, not CI gates**. This matches the current performance redesign: hosted runners may collect trends, while adopter-facing frame-budget claims come from controlled hardware.

CI initially does only compile/smoke, fake-clock tests, schema validation, flow identities, small sliced-vs-contiguous correctness fixtures, and workload-catalog drift checks. No PR/main job fails because a hardware budget completed fewer operations than a timing threshold.

Store results in a distinct versioned namespace, e.g.:

```text
budgeted-progress/v1/<machine-fingerprint>/<commit>/<run-id>/...
```

Reuse campaign metadata/publishing conventions, but do not force this schema through the current Google-Benchmark baseline index without a deliberate publisher extension.

## 15. Staged implementation and acceptance criteria

### Stage 1 — support model, schema, fake clock

Implement benchmark-only frame budget configuration, trace/flow/deadline tracking, clock abstraction, schema, and summary derivation.

**Accept when:** all section 13 tests pass; frame/tick budget scopes are encoded/tested; canonical budgets are exact ns; flow identities hold after every scripted transition; malformed/unknown semantic schema versions fail closed; percentile fields suppress undersampled values; no production API behavior changes.

### Stage 2 — isolated current-capability runner

Add `tess_bench_budgeted_progress`, canonical isolated cells, saturated/rate modes, and contiguous references.

**Accept when:** each cell validates correctness before timing; each result maps to the workload/scenario catalog; synchronous operations only stop between whole calls; real `ResumableWorkQueue` work resumes across frames; every declared sliceable operation passes equivalence; non-resumable operations remain explicit; timer/controller overhead is emitted; timing/counter passes replay the same trace hash; no timing threshold gate is added.

### Stage 3 — 60 Hz mixed colony

Share/refactor colony support as needed, drive 60 Hz frames with existing accumulator, add steady ping-pong demand, deadlines, current-fidelity/existing-quanta views, and population sweep.

**Accept when:** the 512x512/100 reference reproduces existing deterministic setup/dependency order before steady-demand extensions; no point under-places agents; 20/30/60/120 TPS use the existing accumulator; headline mode shares one budget across a frame's ticks; current-fidelity never illegally defers tick-coupled work; every deferred item has an existing safe boundary; flow identities hold; all requested metrics are emitted; drained equivalence passes wherever claimed.

### Stage 4 — controlled campaign integration

Wire into existing development-machine/Steam Deck/cloud-metal workflows as appropriate and add curve summaries.

**Accept when:** one controlled machine can run all seven budgets for at least 20 and 60 TPS and emit valid v1 artifacts; timing is separate from PMU/diagnostics; power/affinity/toolchain/clock metadata is complete where available; canonical sample counts are met or flagged insufficient; repetition boundaries are preserved; CSV/Markdown curves regenerate solely from artifacts; public data is sanitized; results remain advisory campaign data.

### Stage 5 — evidence-driven continuation proposals

Only after stage 2-4 results, identify operations where indivisible quantum duration dominates useful capacity/overshoot.

A separate continuation proposal must document: affected budgets/workloads; p95/p99/max quantum and overshoot on relevant hardware; expected benefit; persistent state/memory; version invalidation; deterministic ordering/equivalence; cancellation/failure/flow semantics; and a contiguous-vs-resumed correctness oracle. Benchmark convenience alone is insufficient justification.

## 16. Risks

| Risk | Mitigation |
| --- | --- |
| Timer overhead matters at 0.125 ms | Calibrate/report clock/controller cost |
| Large synchronous quantum blows small budgets | Treat it as the result; never fake preemption |
| Slicing changes batch/cache algorithm | Forbid partition-as-slicing |
| Deferral changes simulation semantics | Mandatory current-fidelity work stays synchronous |
| Counters distort timing | Separate timing/counter/PMU passes |
| Parallel TLS counters miss workers | Per-worker/per-op post-join reduction or verified serial attribution |
| Cold start contaminates steady curves | Explicit warmup; separate cold-start mode |
| DVFS/SMT/NUMA/thermal modes bias curves | Controlled metadata/pinning/power policy; rotated order; repetitions |
| p99 under-sampled | Minimum sample rules and long confirmation cells |
| Capacity search hides failures | Persist every tested point |
| "Backlog" becomes competing semantics | Derive only from existing `FlowCounters` |
| Colony goes idle | Deterministic goal renewal |
| Matrix explodes | Small canonical serial set, then targeted capacity search |

## 17. Open decisions

1. Canonical TPS: recommend `{20, 30, 60, 120}`, with 20 and 60 required in the first campaign.
2. Pool coverage: serial reference first; then one controlled machine-specific physical-core-conscious worker width after parallel counter aggregation is trustworthy.
3. Mixed deadline: recommend 32 ticks for the 24-tile ping-pong goal; decide later whether summaries also need a tighter path-plan deadline.
4. Supported-population policy: v1 reports flow-stable population plus overshoot rather than inventing a universal frame-safe overshoot tolerance.
5. Raw storage: existing long-retention data branch versus campaign artifact store; schema remains independent.
6. Shared harness code location: extract only deterministic support needed by tests/benchmarks; keep it out of public Tess API.
7. Continuation priority: decide from measurements, not expectation; path/runtime batches, fields, and topology are candidates.

## 18. Recommendation

The adopter-facing result should read like:

> On machine M at 60 FPS / 20 TPS, with a 1 ms **per-frame** Tess budget, workload W sustains X navigation goals/s (or N agents), Y% meet their simulation deadline, outstanding inventory is stable, and p99 cooperative overshoot is Z us.

That is more useful than another standalone microsecond latency while remaining honest about what Tess can interrupt today.

Use a **separate `tess_bench_budgeted_progress` binary**, publish **controlled campaign artifacts/capacity curves rather than initial CI timing gates**, and let the measurements determine whether any separately justified production continuation API is worth adding.
