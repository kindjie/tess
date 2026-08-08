# Budgeted-progress benchmarks

Status: **Proposed design, revision 3; no implementation**

Audited against `main` at `bd304e73063bbb17561180f578d6d81ca2979096`; re-checked against `b613f22` (#108, CI cache/sentinel remediation), which does not affect any claim here.
Revision 2 fixed factual errors against the audited commit and resolved review findings: frame pacing, completed-to-stale reclassification, percentile sample bases, capacity boundary policy, multi-tick overshoot attribution, counter-pass semantics, equivalence under churn, and per-class artifacts.
Revision 3 resolves the final-gate review: overhead-invariant cross-pass comparison for saturated cells, saturated trace/pool/admission semantics, inapplicable-field encoding, artifact granularity, capacity-band edge definition, completion counting bases, and a service-order test.

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
| `Schedule`, `BackgroundBudget` (`sim/schedule.h`); `ResumableWorkTask` (`sim/async_work_task.h`) | Existing task/tick semantics |
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

### 3.2 Frame pacing

Every cell declares one of two pacing modes; the mode is recorded in the artifact and pacing modes are never mixed in one published curve.

**`pacing = unpaced`** runs frames back to back with no inter-frame wait. This is the default for isolated Experiment A cells: it maximizes samples per wall second and avoids sleep/spin jitter. Unpaced cells report per-frame and per-simulation-second metrics only. They must not publish per-wall-second rates: with a 0.125 ms budget an unpaced loop runs thousands of frames per wall second, so completions-per-wall-second would collapse toward saturated throughput and erase the budget axis. Where a wall-rate figure is wanted for presentation it is derived as `per_frame * frame_hz` and labeled `derived_at_60fps`, never measured.

**`pacing = paced`** spins or sleep-spins to each 16.666... ms frame edge on the monotonic clock and records every actual frame start. This is required for the mixed colony (Experiment B) and for any cell that publishes measured per-wall-second metrics. If a frame's work runs past its frame edge, the next frame starts late; the lag is recorded as `frame_start_lag_ns` per frame. Frame allowances are per frame and never carry debt: overshoot is recorded (section 3.4) but does not reduce the next frame's allowance in v1.

In both modes v1 feeds the accumulator the constant `1.0 / 60.0`, not measured deltas. This is deliberate: it keeps tick grants deterministic and identical across budgets and machines, at the cost of excluding feedback dynamics (a slow frame producing a larger delta, more granted ticks, and potentially a death spiral). `FixedStepFrame.dropped_seconds` therefore stays zero by construction in v1; a future measured-delta mode for studying catch-up/death-spiral behavior is an explicit open decision (section 17), not an implicit property of these results.

### 3.3 Per-frame versus per-tick budgets

The headline mode is:

```text
budget_scope = frame
```

One rendered frame gets **one** allowance `B_frame`, shared by every simulation tick granted during that frame. If 120 TPS yields two ticks in a 60 FPS frame, the total entitlement is still `B_frame`, not `2 * B_frame`. **Every rendered frame receives `B_frame`, including frames that grant zero ticks** (at 20 TPS / 60 FPS, two of three frames): defer-capable work may be serviced in tickless frames, matching the section 4 controller. A completion in a tickless frame is attributed to the current (last-granted) `SimClock` tick for deadline purposes.

A secondary mode may use:

```text
budget_scope = tick
```

where each granted simulation tick gets `B_tick`. This can consume approximately `N * B_tick` in a frame with `N` ticks and therefore answers a different question. Never merge per-tick results into the adopter-facing frame-budget curve.

### 3.4 Mandatory work and multi-tick ordering

Current tick-coupled work that cannot safely defer runs when due even after the nominal frame allowance is exhausted; the excess is recorded as overshoot. Defer-capable work may remain outstanding.

When a frame grants multiple ticks under `budget_scope = frame`, the driver runs **all granted ticks' mandatory work first**, in tick order and preserving `SimPhase` ordering within each tick, before starting any budget-metered defer-capable quantum. Otherwise tick 1's optional work could consume the shared allowance and force tick 2's mandatory work into overshoot — an overshoot any real host would trivially avoid by ordering, which would make the number meaningless. In `mixed_current_fidelity`, where defer-capable work is not separately metered, ticks simply run whole in production order.

Overshoot is recorded in two attributed buckets, because they are different phenomena with different remedies:

- `overshoot_quantum_tail`: the deadline fell inside the last defer-capable quantum, which ran to its normal return. Evidence about cooperative interruption granularity (stage 5 input).
- `overshoot_mandatory`: mandatory tick-coupled work executed after the allowance was already exhausted. Evidence about mandatory work volume versus budget, not about granularity.

Both buckets carry per-operation-class attribution in the raw sidecar so stage 5 can identify which operations' quanta dominate. In `mixed_current_fidelity`, where nothing may defer and ticks run whole, all overshoot is by definition `overshoot_mandatory`; the split is informative only where the driver meters defer-capable work.

`BudgetPolicy` is intent metadata, not evidence that a consumer is currently resumable or wall-clock-budgeted. Capability comes from the actual API boundary.

## 4. Honest cooperative enforcement

The wall-time controller belongs in the benchmark driver, not production `Schedule`.

```text
frame_start = clock.now()
deadline = frame_start + budget

# all granted ticks' mandatory work runs first (section 3.4);
# this loop then meters only defer-capable work

while eligible defer-capable work exists:
    if clock.now() >= deadline:
        break
    execute one existing correctness-preserving quantum to normal return
    account its result

frame_end = clock.now()
overshoot = max(0, frame_end - deadline)
```

A quantum started while time remained always finishes. No callback, A* loop, field build, topology transaction, or parallel phase is interrupted mid-call unless an existing production API already exposes a continuation boundary.

For `ResumableWorkQueue`, the driver may check wall time between `advance()` calls; the finest canonical characterization uses `AsyncWorkBudget{1}` where one item is a valid atomic unit. A single callback can still overshoot. For sub-microsecond items a per-item clock read is a material fraction of measured cost; v1 accepts this and reports it via the section 11.1 calibration, and an every-`k`-items check (with `k` recorded) is an open decision (section 17) rather than a silent implementation choice.

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
| `ResumableWorkQueue` | ticket reaches a terminal state (`Ready` or `Immediate` for success; `Failed`/`Cancelled`/`Superseded`/`Stale` otherwise) | existing `items_done`, offered/consumed units | one callback / `advance()` | **Yes** | yes; reference continuation model |

Terminal-bucket resolution for `ResumableWorkQueue` must come from its attached `FlowAccounting`, not from `advance()` return values: `AsyncAdvanceStats::failed` aggregates `Failed`, `Cancelled`, and `Superseded` into one count, so the six per-bucket terminal outcomes section 8.1 requires are unrecoverable from step stats alone.

If small-budget results are dominated by an indivisible operation, separately justify a production continuation API for that operation. Likely candidates are single-query pathfinding, field/product construction, runtime batches preserving grouping/cache behavior, region-graph transactions, and possibly a persistent queued-plan cursor. Do not create these merely to make the benchmark easier.

Partitioning one 100-request batch into 100 independent calls is **not** evidence that the batch is resumable if it changes grouping/cache behavior.

## 6. Experiment A: isolated capacity

Question: with a permanently available supply of one operation, how many correct results fit in each budget, and what quantum causes overshoot?

### 6.1 Canonical cells

Seven canonical cells. Cells 1-4 and 7 reuse existing workload builders and catalog identities; cells 5 and 6 are **new cells** that must be added to the workload matrix, because the current catalog does not contain them:

1. unit point path, 512x512 / 32x32 chunks, structured room/portal-style layout (existing `path/astar_unit` family);
2. weighted point path, 512x512 / 32x32, weighted structured/sparse-blocker layout (existing `path/weighted_astar_unit` family);
3. existing repeated/shared-goal 100-request batch, kept as one current batch call (existing `path/astar_batch` / `path/distance_field_batch` families);
4. field-product build, 512x512, eight deterministic goals (existing `path/field_product` family);
5. **new:** colony-derived incremental topology update with four deterministic dirty chunks, patterned on the colony test harness's churn events — the existing `topology/region_graph` family's only incremental-update cell is `region_graph_update_single_chunk_512x512`; proposed identity `topology/region_graph_update_colony_4chunk_512x512`;
6. **new:** queued one-op-per-chunk update path including planning/execution/dirty merge, patterned on the colony test harness ("one queued operation per distinct chunk") — the existing `queued/execute_resident_update` cells enqueue one operation spanning all resident chunks, the opposite granularity; proposed identity `queued/execute_per_chunk_colony_512x512`, which needs a **new family entry** (the existing `queued/execute` family's pattern and 1024x1024/64x64 defaults do not extend to it, unlike the topology identity, which fits its family's size-capture convention);
7. a real `ResumableWorkQueue` workload to validate actual continuation behavior (existing `resumable_work_step` cell of the `scheduler/tick` family).

Generate a large frozen request pool before timing using the existing deterministic generator/oracle machinery and seed `0x5C0107`. For point queries keep at least 10,000 requests available. No random generation, parsing, allocation setup, or oracle work occurs inside the Tess service timer.

### 6.2 Modes

**Saturated:** eligible inventory never empties. The frozen pool is **inventory, not admitted flow**: an item is offered and admitted at the moment the driver selects it for service, so saturated cells carry no meaningful deadlines, ages, or starvation — those metrics are emitted only by arrival-rate and mixed cells (section 9). Report useful completions per frame and per simulation second, algorithmic work per simulation second, and the overshoot distribution.

Saturated-mode specifics an implementer must not have to guess:

- **Trace identity:** saturated cells have no arrival trace; the `trace` block records the SHA-256 of the frozen pool plus the versioned deterministic selection order in its place.
- **Pool recycling:** service can exceed pool size (an 8 ms unit-A* cell services millions of quanta against a 10,000-item pool); the pool wraps in selection order, and each re-service is a **new admission** of a new flow item, consistent with admit-on-selection. Per-item record buffers are sized from a measured-service-rate bound, not from the frame count.
- **Repeated builds are real builds:** cell 4 must construct the field product through the direct build path with the product cache bypassed or invalidated per iteration — otherwise the cell silently measures cache hits, which the existing `path/field_product` family already covers with separate `cache_hit` cells.
- **`ResumableWorkQueue` admission:** the driver calls `submit()` at selection time, so the queue's attached `FlowAccounting` (which records admission at submit) agrees with admit-on-selection by construction.

Note the flow identities hold trivially under admit-on-selection (`rejected = coalesced = 0`, outstanding near zero); for saturated cells they are a bookkeeping soundness check, not evidence of stability.

**Arrival-rate:** release offers from a deterministic rational-rate accumulator (integer/Bresenham-style, not random sampling). Geometrically bracket and then refine the highest flow-stable rate under the boundary search policy of section 9.3. Save every tested point and trace hash.

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

This anchors directly to `tess_colony_harness_test.cc`'s existing 512x512 / 100-agent / churn-every-8 / four-chunk base configuration. The anchor covers those listed values only: the existing test runs `ticks = 40`, while budgeted cells run for the section 11.4 frame counts (hundreds to thousands of ticks), so run length is a new dimension of this suite, not an anchored one.

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

Ping-pong re-arming makes the mixed colony a **closed-loop** system: each completion generates the next demand, so demand rate is a function of service rate and outstanding inventory is bounded by the population by construction. Consequently the section 9.2 growth criterion cannot signal saturation here — congestion appears as deadline misses, rising residence, and reduced throughput instead. The capacity axis for the mixed colony is therefore **population**, judged primarily by the deadline and age criteria; "sustainable arrival rate" applies only to the open-loop isolated arrival-rate mode (section 6.2). Section 9.2 lists which criteria are load-bearing per experiment type.

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

### 8.2 Per-item records, reclassification, and settlement

`FlowCounters.completed` is **non-monotonic** by documented design: a produced result that later fails its version requirement is reclassified from `completed` to `stale` (`mark_stale_if_version`), so per-window `completed` deltas can decrease. With churn every 8 ticks, reclassification is routine in the mixed colony, not a corner case. Two rules follow:

1. **Snapshot deltas are for conservation only.** Start/end `FlowCounters` snapshots validate the two identities and the inventory metrics. Useful completions and deadline success are **never** derived from `completed` deltas.
2. **Headline metrics come from per-item records.** The driver owns the trace, so it records per item: admission tick, terminal outcome, terminal tick, and any reclassification. After the measured window ends at tick `T_end`, a **settlement period** of `settlement_ticks` simulation ticks follows (canonical: `2 *` the largest scenario deadline allowance, so 64 ticks for the mixed colony) during which no new demand is admitted but reclassifications are still observed. An item counts as a useful, deadline-successful completion only if it completed by its deadline **and** is still valid (not reclassified stale) at settlement close. Reclassifications observed during settlement are attributed back to the admission window before the flow-stability verdict (section 9.2) is computed.

A reclassification after settlement close is out of scope for that cell's verdict; `settlement_ticks` is recorded in the artifact so the cutoff is explicit rather than silent.

### 8.3 Deadlines

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

Every cell emits the metrics applicable to its mode — saturated cells omit the deadline, age, and starvation groups by construction (section 6.2):

- useful completions total, per frame, per simulation second, and per wall second (measured wall rates from paced cells only, section 3.2);
- operation-specific algorithmic work units;
- deadline success rate and lateness p50/p95/p99/max;
- per-frame Tess elapsed time and overshoot p50/p95/p99/max plus overshoot-frame rate;
- `outstanding_current` start/end, high-water, growth rate, tick-weighted inventory;
- oldest outstanding age p50/p95/p99/max;
- terminal outcome counts, admission/rejection/coalescing counts;
- starvation/fairness metrics;
- sustainable arrival rate or population.

A partial A* or partially rebuilt graph never counts as useful completion unless a production API exposes a valid partial result.

Two counting bases apply and every metric names its basis: **throughput metrics** (useful completions total/per frame/per second) count items whose completion tick falls inside the measured window, regardless of admission tick; **cohort metrics** (deadline success, lateness) follow the admitted-in-window cohort through settlement (section 8.2). Section 13 test 21's per-class aggregation is checked on both bases.

### 9.1 Starvation/fairness

An item is reportably **starved** if it remains continuously eligible but receives no service quantum for:

```text
max(4 * deadline_allowance_ticks, one_sim_second_ticks)
```

where `one_sim_second_ticks = base_tps` — one simulation second always contains `base_tps` ticks, since `SimSpeed` scales wall time, not simulation time (`effective_tps(base_tps, speed)` in `include/tess/sim/time.h` is the speed-multiplied *wall*-second rate and equals `base_tps` only at the canonical `Speed1x`). The window is four deadline windows or one simulation second, whichever is longer, measured in simulation ticks while the item is dependency-ready. Report count/max no-service age by priority/class.

For homogeneous multi-producer fairness tests, report Jain's index over producer-normalized useful-completion rates. Do not combine unlike priority classes into one fairness number; mixed workloads report per-class service share and oldest age.

### 9.2 Flow-stable capacity

Take `FlowCounters` snapshots at the start/end of the measured window; derive deadline success from the section 8.2 per-item records after settlement. With `A` measured-window admissions, a point is **flow-stable** when:

1. both conservation identities hold;
2. positive outstanding growth is at most `max(1, ceil(0.005 * A))` items (0.5% plus one-item integer slack);
3. final oldest outstanding age does not exceed the starvation window, evaluated **per class** against each class's own window (section 9.1) — any class exceeding its window fails the criterion;
4. the scenario deadline target is met.

Canonical deadline target: **99%** successful completion by deadline. Raw data must allow 95%/99.9% summaries when sample counts support them.

Criteria applicability differs by experiment type. In the open-loop arrival-rate mode all four criteria are load-bearing. In the closed-loop mixed colony (section 7.2), criteria 1-2 are satisfied nearly by construction — outstanding inventory is bounded by the population — so the verdict rests on criteria 3-4; report criteria 1-2 anyway as validity checks, and key the capacity verdict off the interactive navigation class (section 12 per-class records), not a pooled all-class rate.

Do not bake a universal overshoot tolerance into "sustainable" in v1. A load can be flow-stable while producing unacceptable frame spikes. Put p99/max overshoot beside every maximum flow-stable arrival rate/population. A future product policy may define a separate "frame-safe" threshold.

A quiescent drain may be used for correctness equivalence but never to erase measured-window outstanding growth.

### 9.3 Capacity boundary search policy

Flow-stability near the boundary is a noisy binary outcome, not a monotone property of load: criteria 2 and 4 both flip on a handful of items, and observing stable-at-`r` / unstable-at-`r' < r` is expected, not an error. The search therefore has an explicit policy rather than assuming bisection converges:

- **Probe verdict:** each probe point runs 3 repetitions of the warm-throughput cell configuration (120 warmup / 600 measured frames, section 11.4); the point's verdict is the majority (2 of 3). Every repetition is persisted regardless of verdict.
- **Bracket and refine:** geometric bracketing on probe verdicts, then linear refinement down to a terminal resolution — 2% of rate for arrival-rate mode, one population-ladder step for the mixed colony. Refinement below the resolution stops even if verdicts still flap; flapping is reported, not hidden.
- **Confirmation:** the highest stable probe runs the capacity-boundary confirmation cell (section 11.4: 1800 frames, 5 repetitions, majority verdict). If confirmation **fails**, that point is recorded as unstable and the search steps down one resolution unit and re-confirms; it never re-runs confirmation at the same point hoping for a different answer.
- **Reported result:** capacity is a **band**, not a point — the highest confirmed-stable load, and the lowest observed-unstable load **above** it. Unstable observations below the confirmed-stable point are expected near a noisy boundary; they are retained in the raw data and reported as verdict flapping, but they do not define the band edge, so the band cannot invert. The two edges carry unequal evidence (5-repetition confirmation versus 3-repetition probe) and are labeled accordingly. Summaries may headline the confirmed-stable value but must carry the band.
- **Probe percentiles:** probe runs pool 1800 frames (3 x 600), below the p99 minimum, so probe artifacts publish frame percentiles at p50/p95 only; p99 figures come from the confirmation cell.

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

Strict result equivalence is claimed only for **frozen-world fixtures**, where no versions change while work is pending and checkpoints are trivially identical. Under churn, bitwise equivalence is the wrong oracle: churn fires at fixed simulation ticks while the sliced run services a given request several ticks later than the contiguous run, so the same request can legitimately see a different world version and terminate stale in one run and completed in the other — a scheduling difference, not a slicing bug. Churn fixtures instead verify that both runs preserve the conservation identities and that every produced result is valid against the world version it was produced under. Where a stronger comparison is wanted, an equivalence fixture may use **progress-keyed churn** — applying churn event `k` only when each run has serviced the same item count — as an explicitly benchmark-only fixture policy that must never leak into measured cells.

Forbidden shortcuts: splitting a batch into a different batching strategy, creating a benchmark-only tile/chunk field algorithm, publishing partial topology, reordering requests for locality, or freezing updates only in sliced mode. If equivalence needs a new continuation API, mark the current operation non-resumable.

## 11. Timing methodology

### 11.1 Clock and observer cost

Use a monotonic clock (`steady_clock` unless a controlled platform selects another). Record clock identity/resolution, clock-read median/p95 cost, and empty controller-loop cost. This matters at 0.125 ms.

Pre-generate trace data outside the Tess service timer. The primary service timer includes budget-controller checks, Tess calls, and necessary deterministic service-selection bookkeeping; it excludes trace generation/parsing, oracle work, serialization, and summary/histogram construction.

### 11.2 Timing versus instrumentation

Follow existing campaign practice:

1. **timing pass:** minimum instrumentation; only cheap boundary-returned counters;
2. **counter pass:** identical demand-trace hash, detailed deterministic counters;
3. optional **PMU pass:** controlled hardware only, never the source of published wall time.

An identical demand trace does **not** make the counter pass execution-identical to the timing pass: which quantum lands in which frame depends on live clock readings, so the two passes realize different service schedules. The counter pass is therefore *statistically comparable, not replayed*: frame-level joins between passes are forbidden, and counter-pass results are compared to timing-pass results only in distribution. The v1 comparison set and default tolerances, recorded in the artifact and revisitable after the first campaign, differ by cell mode because saturated throughput is **not overhead-invariant**: in a saturated cell, completions per frame equal roughly budget over per-quantum cost, so the counter pass — whose per-node instrumentation is a large multiple of a ~2 ns A* expansion step — completes systematically fewer items by construction, and comparing completions between passes would gate on exactly the distortion the pass separation exists to isolate.

- **Saturated (budget-limited) cells** compare overhead-invariant quantities only: per-item result validity, both conservation identities holding in both passes, and an **exact zero-tolerance work identity enforced inside each binary** — window consumed work units must equal the contiguous reference's prefix sum over the serviced pool range (pool-serviced cells) or completions times the reference build's work (constant-work cells). The statistical 1% work-units-per-completion cross-pass gate applies only when both passes completed at least one full pool wrap: below a wrap the ratio measures pool-prefix composition, not instrumentation divergence. Completions are reported from both passes but never gated.
- **Demand-limited cells** (arrival-rate below saturation, mixed) compare useful completions and consumed work units within 5% relative, both conservation identities, and per-class deadline success within 2 percentage points. If exact annotation of a timing run is ever needed, the timing pass must record its quantum schedule (frame -> serviced items) and the counter pass must replay that recording; whether to build schedule recording is an open decision (section 17).

For the same reason `summary.correctness_hash` has a defined expected value only for contiguous references and fake-clock runs, where execution is deterministic; wall-driven cells emit `null` there and rely on the per-item validity checks and conservation identities instead.

Do not publish timing gathered under diagnostics allocation hooks or `perf` wrapping.

### 11.3 Parallel counter aggregation

The compile-gated queued/phase counter sinks are thread-local (`active_queued_phase_counters`): pool-worker counters do not automatically aggregate into the caller sink. (`FlowAccounting` is different — caller-owned serial accounting that concurrent flows must synchronize themselves.) Never publish incomplete parallel counts.

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

Cold-start is separate: 20 repetitions, each a fresh process with a freshly constructed scenario, no warmup, first 120 frames retained. "Cold" is defined explicitly: Tess-internal state (field-product cache, topology graphs, path runtime caches) starts empty; the demand pool is still pre-generated before timing (section 6.1), so allocator state, OS page cache, and pool memory touched by generation are incidentally warm, and the artifact says so.

Minimum sample counts before publishing a percentile:

```text
p50:   20
p95:   200
p99:   2000
p99.9: 20000
```

Otherwise emit `insufficient_samples`. Each published percentile names its sample base explicitly:

- **Frame elapsed and overshoot percentiles** pool all measured frames across repetitions (warm cell: 6000; confirmation cell: 9000), with repetition boundaries preserved in the raw sidecar. Overshoot percentiles are computed over **all** measured frames with non-overshoot frames contributing zero; `overshoot_frame_rate` is reported beside them, and a conditional distribution over overshoot frames only is additionally published when that subset alone meets the sample minimums, else marked `insufficient_samples`. Published p99s are pooled by necessity — a 600-frame repetition cannot support a per-repetition p99 — so per-repetition summaries are limited to p50/p95.
- **Lateness percentiles** are conditional on completion: the population is deadline-carrying items admitted in the measured window that reach `completed` (and survive settlement). Items that terminate otherwise or remain outstanding are **excluded**, not censored to infinity — they are captured by the deadline success rate, which counts them as misses; the two metrics are always published together so the survivorship restriction is visible. Cells that complete fewer than 2000 such items emit `insufficient_samples` at p99 rather than a fabricated figure.
- **Repetition-level variability** is reported as the median of per-repetition medians with min/max across repetitions. Ten repetitions support a spread statement, not a confidence interval; no CI is claimed.

During measured frames the driver performs no allocation, logging, or I/O: in-run samples land in preallocated ring buffers sized from the frame count — or from the measured-service-rate bound for per-item records in saturated cells (section 6.2) — and are flushed outside the timer.

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
    "workload_refs": ["path/astar_unit", "topology/region_graph", "queued/execute"],
    "seed": 6029575,
    "frame_hz_num": 60,
    "frame_hz_den": 1,
    "sim_tps": 20,
    "sim_speed": "1x",
    "max_ticks_per_frame": 8,
    "pacing": "paced",
    "budget_scope": "frame",
    "budget_ns": 1000000,
    "settlement_ticks": 64,
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
  "classes": [
    {
      "class_id": "interactive_path",
      "deadline_allowance_ticks": 32,
      "flow": {"...": "same shape as top-level flow"},
      "useful_completions": 0,
      "deadline_success_rate": 0.0,
      "lateness_ticks_p99": 0,
      "starved_items": 0,
      "oldest_age_ticks_p99": 0,
      "service_share": 0.0
    }
  ],
  "summary": {
    "measured_frames": 600,
    "useful_completions": 0,
    "deadline_success_rate": 0.0,
    "overshoot_frame_rate": 0.0,
    "overshoot_quantum_tail_ns_p99": 0,
    "overshoot_mandatory_ns_p99": 0,
    "frame_start_lag_ns_p99": 0,
    "backlog_growth_per_sim_second": 0.0,
    "oldest_age_ticks_p99": 0,
    "starved_items": 0,
    "fairness_jain": null,
    "flow_stable": false,
    "capacity_band": {"confirmed_stable": null, "lowest_unstable": null},
    "peak_rss_bytes": 0,
    "correctness_hash": null
  }
}
```

The example is **abridged**, not exhaustive: each percentile family (frame elapsed time, overshoot buckets, lateness, ages) emits p50/p95/p99/max variants plus a `sample_base` discriminator naming its population (section 11.4); flow snapshots are recorded at both window start and end; per-frame series and per-item records live in the sidecars. `workload_refs` must use existing catalog identities (family or cell names), never a second taxonomy.

Encoding rules the example does not show: **one artifact per cell**, aggregating all repetitions, with per-repetition summaries in the sidecar; `flow_stable` is the cell's majority verdict (section 9.3), not a single repetition's; `capacity_band` is populated only in the search-summary artifact that spans cells and is `null` in individual cell artifacts. Inapplicable metric groups are **omitted entirely, never emitted as zero**: a saturated cell carries no `deadline_success_rate`, lateness, age, starvation, or `classes[]` fields (a `0.0` there would read as a 100% miss rate), and sets `settlement_ticks: 0` since no deadline allowance exists to settle. The top-level `flow` block and `summary` totals aggregate all classes; every multi-class cell also emits one `classes[]` entry per demand class, since a pooled deadline-success rate over classes whose allowances span 1 to 20+ ticks is not interpretable, and the mixed-colony capacity verdict keys off the interactive class specifically (section 9.2). Seeds are recorded in decimal in artifacts; prose may add the hex form. `frame_start_lag_ns_*` fields appear only for `pacing = paced`; `correctness_hash` is non-null only for contiguous references and fake-clock runs (section 11.2); `peak_rss_bytes` is sampled at repetition boundaries outside the timer, because a "flow-stable" verdict beside monotonically growing memory would be misleading.

Additive fields may remain v1; semantic changes require v2. Raw frame/request samples — including per-frame overshoot attribution by operation class (section 3.4) and per-repetition boundaries — live in compressed sidecars keyed by the same run/cell/trace identity.

Generate concise CSV/Markdown capacity rows:

```text
scenario | sim_tps | budget_ms | pacing | useful_per_frame |
confirmed_stable_rate_or_population | lowest_unstable |
deadline_success | backlog_drift/s | p99_overshoot_us | max_oldest_age_ticks
```

Isolated headline: useful completions per frame (and per second only for paced or explicitly derived figures, section 3.2) versus budget. Mixed headline: the confirmed flow-stable population band versus budget, always beside world size, TPS, executor, machine fingerprint, interactive-class deadline success, and p99 overshoot by bucket.

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
16. large nanosecond values do not underflow elapsed/overshoot arithmetic, and overshoot at or before the deadline is exactly zero via explicitly non-negative arithmetic (no unsigned wrap);
17. a completion reclassified stale during the window or settlement is removed from useful completions and deadline success and attributed to its admission window; the negative per-window `completed` delta does not corrupt derived metrics;
18. a reclassification after settlement close does not alter the sealed verdict;
19. paced mode records `frame_start_lag_ns` when a frame overruns its edge, and the next frame's allowance is unreduced; unpaced mode emits no measured per-wall-second rates;
20. with multiple granted ticks, all ticks' mandatory work runs before any defer-capable quantum, and overshoot lands in the correct attribution bucket (`quantum_tail` versus `mandatory`);
21. per-class summaries aggregate exactly to the cell totals for flow counts, useful completions, and starvation;
22. a frame granting zero ticks still receives the full frame allowance and may service defer-capable work; its completions are attributed to the last-granted tick;
23. the section 4.1 service order is exercised through the full tie-break chain: dependency-readiness gates, then `Priority`, then earliest inclusive deadline, then admission sequence — each tie broken at exactly the documented level.

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

Initial real-time results should be **controlled campaign artifacts, not CI gates**. To be explicit about current repo policy: production benchmark families **are** gated in CI today with calibrated per-benchmark ceilings (`docs/performance.md`, `bench/thresholds/*.json`) on the full-tier Linux runners; the paired sentinel run (shadow mode), counter-golden drift checks, `lab/` families, and controlled campaigns are non-gating, and benchmark threshold gating is entirely absent from the PR tier and macOS jobs (which gate builds and tests, not timing). This suite therefore proposes a deliberate **exception** to that policy, not a continuation of it, justified on three grounds: a multi-frame queueing experiment has no single-number ceiling to calibrate; its results are wall-clock- and machine-dependent in ways the existing runner-specific ceilings do not model; and capacity search is far too expensive for per-PR execution. If the suite later yields a stable, cheap, single-cell smoke number, promoting that one number into the existing threshold machinery is a separate decision.

CI initially does only compile/smoke, fake-clock tests, schema validation, flow identities, small sliced-vs-contiguous correctness fixtures, and workload-catalog drift checks. No PR/main job fails because a hardware budget completed fewer operations than a timing threshold. The existing sentinel/threshold machinery is intentionally out of scope for this suite's artifacts.

Store results in a distinct versioned namespace, e.g.:

```text
budgeted-progress/v1/<machine-fingerprint>/<commit>/<run-id>/...
```

Reuse campaign metadata/publishing conventions, but do not force this schema through the current Google-Benchmark baseline index without a deliberate publisher extension.

## 15. Staged implementation and acceptance criteria

The first implementation iteration is deliberately narrower than the full matrix: **stage 1 plus the saturated subset of stage 2** — three operations (unit A*, field-product build, `ResumableWorkQueue`), budgets `{0.5, 2, 8}` ms plus `0.125` ms as a granularity probe, serial executor, one machine, no capacity search. That validates the controller, overshoot semantics, and schema against reality before spending on arrival-rate search, the mixed colony, and the seven-budget x TPS x view matrix. The remaining stages follow only after that subset produces sane artifacts.

### Stage 1 — support model, schema, fake clock

Implement benchmark-only frame budget configuration, pacing modes, trace/flow/deadline tracking with per-item records and settlement, clock abstraction, schema (including per-class blocks), and summary derivation.

**Accept when:** all section 13 tests pass; frame/tick budget scopes and both pacing modes are encoded/tested; canonical budgets are exact ns; flow identities hold after every scripted transition; reclassification and settlement behave per section 8.2; malformed/unknown semantic schema versions fail closed; percentile fields suppress undersampled values and name their sample base; no production API behavior changes.

### Stage 2 — isolated current-capability runner

Add `tess_bench_budgeted_progress`, canonical isolated cells, saturated/rate modes, and contiguous references.

**Accept when:** each cell validates correctness before timing; each result maps to the workload/scenario catalog (including the two new cells added to the matrix, section 6.1); synchronous operations only stop between whole calls; real `ResumableWorkQueue` work resumes across frames; every declared sliceable operation passes equivalence; non-resumable operations remain explicit; controller overhead is bounded and reported with the measurand named: the **empty controller-loop cost per frame** (section 11.1 calibration: service selection plus one clock read, no Tess work) stays below 5% of the smallest canonical budget on the reference machine, while the **aggregate clock-read share** under load is emitted per cell but not bounded — for `AsyncWorkBudget{1}` cells section 4 accepts it as material, and open decision 9 is the remedy if it dominates; timing and counter passes consume the identical demand trace and agree in distribution within the section 11.2 tolerances; no timing threshold gate is added.

The two new colony-derived cells (section 6.1 items 5-6) depend on colony-support extraction that stage 3 otherwise owns; full stage 2 pulls that extraction forward for those two cells only, and the first implementation iteration avoids the dependency entirely by starting with cells 1, 4, and 7.

### Stage 3 — 60 Hz mixed colony

Share/refactor colony support as needed, drive 60 Hz frames with existing accumulator, add steady ping-pong demand, deadlines, current-fidelity/existing-quanta views, and population sweep.

**Accept when:** the 512x512/100 reference reproduces existing deterministic setup/dependency order before steady-demand extensions; no point under-places agents; 20/30/60/120 TPS use the existing accumulator; headline mode shares one budget across a frame's ticks with mandatory-first ordering (section 3.4); current-fidelity never illegally defers tick-coupled work, checked operationally by asserting the per-tick executed-task sequence matches an unbudgeted reference run of the same trace; every deferred item has an existing safe boundary; flow identities hold; all requested metrics including per-class blocks are emitted; drained equivalence passes wherever claimed under the section 10 churn rules.

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
| Completed-to-stale reclassification corrupts windowed metrics | Per-item records with settlement (section 8.2); snapshots for conservation only |
| Pacing mode changes wall-second/thermal semantics | Explicit per-cell pacing declaration; no measured wall rates from unpaced cells |
| Closed-loop colony makes growth criterion vacuous | Per-experiment criteria applicability (section 9.2); population as the capacity axis |
| Capacity verdict flaps at the boundary | Majority probes, confirmation step-down, band reporting (section 9.3) |

## 17. Open decisions

1. Canonical TPS: recommend `{20, 30, 60, 120}`, with 20 and 60 required in the first campaign.
2. Pool coverage: serial reference first; then one controlled machine-specific physical-core-conscious worker width after parallel counter aggregation is trustworthy.
3. Mixed deadline: recommend 32 ticks for the 24-tile ping-pong goal; decide later whether summaries also need a tighter path-plan deadline.
4. Supported-population policy: v1 reports flow-stable population plus overshoot rather than inventing a universal frame-safe overshoot tolerance.
5. Raw storage: existing long-retention data branch versus campaign artifact store; schema remains independent.
6. Shared harness code location: extract only deterministic support needed by tests/benchmarks; keep it out of public Tess API.
7. Continuation priority: decide from measurements, not expectation; path/runtime batches, fields, and topology are candidates.
8. Measured-delta pacing mode: whether/when to add a mode that feeds real frame deltas to the accumulator to study catch-up and death-spiral behavior (section 3.2); v1 is fixed-delta only.
9. Clock-check granularity for sub-microsecond resumable items: per-item in v1; an every-`k`-items check with recorded `k` if calibration shows per-item reads dominate (section 4).
10. Quantum-schedule recording: whether the timing pass should record its frame-by-frame service schedule to enable exact counter-pass replay, versus the v1 distribution-comparison rule (section 11.2).
11. Settlement window length: canonical `2 *` largest deadline allowance (section 8.2); revisit if measured reclassification tails prove longer.
12. Capacity search terminal resolution values (2% rate / one ladder step, section 9.3): confirm after the first campaign's boundary-noise data.

## 18. Recommendation

The adopter-facing result should read like:

> On machine M at a paced 60 FPS / 20 TPS, with a 1 ms **per-frame** Tess budget, workload W sustains X navigation goals/s (or a confirmed-stable population of N agents), Y% of interactive requests meet their simulation deadline, outstanding inventory is stable, and p99 cooperative overshoot is Z us (quantum-tail).

That is more useful than another standalone microsecond latency while remaining honest about what Tess can interrupt today.

Use a **separate `tess_bench_budgeted_progress` binary**, publish **controlled campaign artifacts/capacity curves rather than initial CI timing gates**, and let the measurements determine whether any separately justified production continuation API is worth adding.
