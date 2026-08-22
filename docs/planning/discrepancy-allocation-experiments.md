# Discrepancy-aware allocation experiments

Status: **Proposed research program** (2026-08-21), revised once after an
independent technical review. This document records a candidate experiment
program, its feasibility against the current codebase and hardware, and the
gates each experiment must pass. It is not current architecture, a roadmap
commitment, an accepted design, or permission to add public API.

The maintained [v0.13-to-v1.0 execution plan](v0.13-to-v1.0-execution-plan.md)
keeps ordering authority over the pre-1.0 pipeline and fixes the complete
bounded prototype queue. Nothing here joins that queue or gates `v1.0.0`, and
nothing here starts before that queue is dispositioned unless a maintainer
amends the plan and records the amendment. Where the two contend for the
serialized controlled-hardware campaign queue, the execution plan wins.

The [roadmap](../roadmap.md) remains the authority for released,
release-gated, deferred, and out-of-scope work. Maintained
[architecture](../architecture/README.md) notes remain the authority for
current behavior.

This program adds no rulebook of its own. The prototype rules in the execution
plan and the equivalent checklist in the
[pathfinding prototype survey](pathfinding-prototype-survey-2026-08-18.md)
govern every experiment here: one hypothesis, the smallest private
implementation that can test it, a canonical baseline and an interleaved
control, an independent correctness oracle, a noise-derived go/no-go threshold
declared before results, and one
[optimization-log](optimization-log.md) fragment per completed experiment
whether accepted, rejected, or deferred. The sections below add only what is
specific to allocation-policy experiments.

## The question

Several Tess allocation problems share a shape: more candidate work exists
than a budget admits, and something must choose. The program asks whether one
accumulated-deficit rule — select against `actual_service - ideal_service`
rather than arrival order, age, or chance — beats simpler policies across
enough of those problems to justify a shared mechanism.

The null result is the expected one. Most of these experiments should end in
`discard`, and the program is arranged so a discard is cheap and early rather
than a sunk cost.

**The order of reasoning is the program's governing rule.** Work from an
observed pathology to an offline counterfactual, then to the smallest
mechanism that addresses it, then to an endpoint independent of that
mechanism. The inverse — start from an interesting algorithm and look for a
benchmark where it wins — produces results that are true about the benchmark
and silent about Tess. Every gate below exists to enforce that direction, and
an experiment that cannot name the pathology it addresses does not open.

## Prior evidence this program must start from

Two results already bear directly on it, and neither is optional context.

**Cheap invalidation scheduling already lost once.** MNT-3 froze and measured
the dirty-bit maintenance scheduler — the simplest form of "schedule
invalidation work cleverly" — against immediate execution. The M3 leg was
flat; the SteamOS-class leg materially regressed in budgeted, flush, and
256- and 1,024-task scaling workloads. The cross-hardware rule therefore kept
that backend experimental, and `v0.13.0` graduated a stable contract whose
default is synchronous immediate execution. The
[evidence bundle](evidence/v0.13/maintenance/README.md) is the starting point
for any successor experiment, which must pre-register why deficit bookkeeping
would escape the overhead trap that the cheaper mechanism did not.

**The incumbent service order is already a deadline policy.** The
budgeted-progress suite's documented order selects dependency-ready items,
then `Priority`, then earliest inclusive simulation deadline, then admission
sequence. That is earliest-deadline-first within priority, not FIFO. Any
experiment naming "the existing Tess scheduler" as a comparison arm is
under-specified; the incumbent is this order, and an "oldest-first" arm risks
being the same policy under a second name.

## Existing substrate

The program reuses what exists. Each experiment names its seam; building a
parallel scheduler, clock, artifact format, or harness is a defect in an
experiment rather than a shortcut.

- **Deterministic tick budget.** `Schedule` background cadences budget in
  **items**, never wall time; `ScheduleContext` reports `budget_items` and
  `items_done` (`include/tess/sim/schedule.h`).
- **Deterministic fractional rates.** `Cadence::every_ticks`, plus the
  suite's rational-rate arrival accumulator.
- **Fixed-step time.** `FixedStepAccumulator` and `run_schedule_frame`
  (`include/tess/sim/time.h`).
- **Cooperative continuation.** `ResumableWorkQueue`
  (`include/tess/ops/async_work.h`).
- **Backlog and staleness accounting.** `FlowAccounting` counters, including
  `outstanding_current` and `oldest_outstanding_age_ticks`
  (`include/tess/sim/event_stream.h`).
- **Wall budget, overshoot, per-item records, settlement.**
  `bench/budgeted_progress_bench_common.h`, designed in
  [budgeted-progress benchmarks](budgeted-progress-benchmarks.md).
- **Artifact validation and summary.** `tools/check_budgeted_artifacts.py`,
  `tools/compare_budgeted_passes.py`, `tools/summarize_budgeted_curves.py`.
- **Registered maintenance and dirty work.** `tess::maintenance` over the
  experimental backends in `include/tess/experimental/`.
- **Byte-budgeted product cache.** `FieldProductCache`, LRU with an
  `evictions` counter (`include/tess/path/field_product_cache.h`).
- **Sparse residency eviction.** Fixed-budget slot LRU
  (`include/tess/storage/residency.h`).
- **Disjoint chunk ownership per phase.**
  `include/tess/ops/phase_executor.h`.
- **Worker scaling sweep.** `tess_bench_thread_scaling`, reported by
  `tools/thread_scaling_report.py`.
- **Paired interleaved A/B.** `tools/paired_bench.py`, `bench/thresholds/`.
- **Metered bare-metal runs.** The
  [cloud campaign runbook](cloud-campaign.md).

The suite also already owns the definitions this program would otherwise
restate badly: what counts as a useful completion, the settlement period
before a completion is judged, the separate throughput and cohort counting
bases, and the rule that measured wall rates come from paced cells only.
Cite them; do not re-derive them.

## Hardware reality

This constrains the program more than any algorithmic question does.

- **Apple M3 (controlled).** One package, unified memory, 12 performance and
  4 efficiency cores. No NUMA domains, and no supported thread-affinity API
  on this platform, so pinning experiments cannot run here even in principle.
- **SteamOS-class APU (controlled).** Four Zen 2 cores by two threads, one
  CCX, one memory controller. No NUMA domains, one shared last-level cache.
- **`c3-standard-192-metal` (metered, best-effort tier).** Intel Xeon
  Platinum 8481C, two sockets by 48 cores by two threads, four NUMA domains
  and 105 MiB of L3 per socket. The only genuine multi-domain hardware in
  reach, at roughly $10 per hour and an explicit maintainer go-ahead per run.
  It is a best-effort server target rather than a third supported platform;
  see the deferred track for what that does and does not promise.

Two consequences are load-bearing:

- **The cross-hardware decision rule cannot be satisfied for NUMA work.** It
  accepts a portable performance change only when one controlled platform
  materially wins and the other does not materially regress. Neither
  controlled platform has NUMA domains, so no NUMA result can pass or fail
  it. The execution plan already anticipates this case: hardware-specialized
  production paths sit outside it and require a separate decision.
- **Core asymmetry is the portable locality dimension available today.** The
  M3's performance and efficiency cores give a real, controlled heterogeneity
  signal that needs no rented hardware. Where a locality question can be posed
  against core asymmetry instead of memory domains, pose it there.

## Rules specific to allocation-policy experiments

**Independent endpoints.** Cumulative discrepancy is what a deficit policy
greedily minimizes, so a deficit policy winning a discrepancy metric is true
by construction and would remain true if the policy were useless. Discrepancy
is a manipulation check and explanatory data, never a primary endpoint.
Primary endpoints must be independent of the selection mechanism: deadline
success, useful completions within budget, staleness and age tails at p95,
p99, and maximum, simulation-result error against a full-service reference,
throughput, backlog stability derived from the existing flow counters rather
than a competing counter, and wall-clock overhead. A deficit policy is
interesting only where reducing discrepancy moves one of those.

The same trap generalizes beyond scheduling: any experiment whose candidate
optimizes quantity X must not be judged primarily on X. Balance is what a
balancing algorithm optimizes; hit rate is what a cache policy optimizes.
Name the downstream consequence instead.

**Equal infrastructure across arms.** At this scale one A* expansion step is a
few nanoseconds and the calibration floor is 25 ns, so a heap in one arm and
an increment in another measures data structures, not policies. Every arm
shares one selection framework with only the key function varying. Scheduler
overhead is measured by the suite's existing method — empty controller-loop
cost and clock-read share — and the suite's separation of the timing pass from
the counter pass applies unchanged.

**Wrong-signal negative control.** Every accepted result must be re-run with
the deficit vector replaced by a shuffled or irrelevant constraint. If the
mechanism still wins, the gain was structural asymmetry between arms, not the
signal, and the result is void.

**Clairvoyant ceiling.** Each comparison reports an offline optimal or
clairvoyant bound on the same trace, so a measured gain can be read against
the headroom that existed rather than against zero.

**Cheap online policies only.** Candidate policies are greedy and online.
Theoretical discrepancy algorithms — semidefinite-programming or
Brownian-motion constructions — are admissible solely as offline reference
implementations computing a bound, alongside the clairvoyant ceiling above,
and never as a candidate for Tess.

**Determinism boundary, declared per policy.** A deficit computed from ticks
and items is deterministic and could in principle be adopted. A deficit or
placement decision computed from measured wall time, observed queue delay, or
device load makes simulation-visible outcomes timing-dependent, which reverses
a documented decision. Every candidate policy declares which side it is on
before it runs; a win for a policy that cannot cross the line is a win for
derived and maintenance work only, never for authoritative simulation.

**Resolution.** Hosted ceilings are twice the observed maximum against 6-15%
coefficients of variation and cannot resolve a few-percent policy delta. Every
comparison here is a paired, interleaved, per-machine noise-thresholded
campaign or it is not a result.

## E0: offline counterfactual replay

Run this before writing any C++. The accepted budgeted-progress artifacts
already carry per-item records: admission tick, service order, terminal
outcome, deadline result, and ages. An offline script can replay a recorded
serial trace under any candidate selection policy and compute deadline
success, staleness, and discrepancy without touching the harness or spending
device time.

If the recorded incumbent order already shows near-zero starvation and tight
age tails on representative workloads, the headroom for every selection-policy
experiment below is bounded, and most of this program should be discarded for
a few hours of Python. E0 is the program's kill-check, and no later experiment
opens until it reports headroom worth chasing.

E0 also settles a question the source program left open: whether "oldest-first"
is a distinct arm at all. It is distinct only where deadline order and age
order diverge in the recorded workloads. The replay establishes that before
anyone implements it.

## Revised order and its relation to the original program

The source program specified eight experiments in strict order. Hardware and
substrate do not support that order; the mapping is explicit rather than
silent.

| Source | Disposition here |
| --- | --- |
| 1 — temporal update coloring | **E1**, absorbing 6, after E0 |
| 2 — NUMA (2A-2K) | **Deferred past 1.0**; one portable fragment kept as E5 |
| 3 — CPU/GPU allocation | **Deferred past 1.0**; precondition unsatisfiable |
| 4 — spatial partitioning | **E4**, static only, contingent on E1 |
| 5 — dirty-region scheduling | **E2**, reframed against the MNT-3 prior |
| 6 — fractional fidelity | **Merged into E1**; the mechanism already exists |
| 7 — multi-objective cache policy | **E3**, scoped to one cache |
| 8 — discrepancy-aware integerization | **Dropped**; recorded below |

### E1: budgeted service selection

The one experiment with a complete substrate waiting for it. Specify it as an
extension of the existing budgeted-progress suite, adding selection policy as
a recorded dimension — the artifact schema already mandates a versioned
service-policy identifier — rather than as a new harness.

Arms: the documented incumbent order; round-robin; random; stride or deficit
round-robin, which is the established cheap deficit scheduler and is the
incumbent *for* the idea rather than a foil; and the candidate multi-constraint
deficit policy. Absorb the fractional-fidelity program as an entitlement mode:
the deterministic rational-rate accumulator already exists, so what remains
untested is holding target rates simultaneously across regions, categories,
and subsystems, which is this experiment's fairness question with fractional
entitlements.

Primary endpoints: deadline success, and simulation-result error against a
full-fidelity reference. That error metric is the only proposed measure fully
independent of the scheduling mechanism, and it is promoted accordingly.
Paired M3 and SteamOS-class campaigns; both legs required for any performance
claim.

### E2: invalidation scheduling

Workloads where one change simultaneously invalidates connectivity, flow
fields, path caches, and derived fields, including severe construction and
destruction bursts. Arms: immediate execution, FIFO, priority,
oldest-dirty-first, and multi-constraint deficit over subsystem and space.

MNT-3 does not prove that all invalidation scheduling is worthless. It
reverses the burden of proof. So E2 opens with its own offline stage rather
than a mechanism: establish from recorded traces that some workload drives
starvation or unbounded backlog that immediate execution and the existing
dirty-bit machinery genuinely fail to handle. No pathology, no E2 — do not
build a more elaborate scheduler in search of a problem. Where a pathology
does exist, "loses to immediate on the SteamOS-class device" remains the
pre-registered expected outcome, and the experiment's value is in explaining
why: whether bookkeeping overhead or work saved dominates.

### E3: residency-cache deficit scoring

Tess's caches are independent, each with its own budget and policy, and no
shared-budget arbiter exists across them. Competing working sets under one
policy would be new architecture, not an experiment parameter, so scope this
to one cache. Choose the one cache whose miss cost is measurable and
production-relevant; sparse residency is the best candidate. Open the
comparison only after showing that multiple working sets genuinely compete in
a representative workload — otherwise there is no allocation problem to solve
and the experiment is a cache-policy exercise with no Tess content.

Arms must include the strong modern baselines, not only plain LRU:
segmented-LRU or 2Q, CLOCK-Pro, and S3-FIFO. Beating plain LRU is a low bar
the literature clears repeatedly without deficit machinery. The recent
intrusive-LRU work also leaves the policy-overhead budget at nanoseconds, so
"materially beats simpler policies" needs a number declared in advance.

### E4: static spatial partitioning

Static chunk-to-worker-group assignment is constructible today because queued
planning already proves disjoint mutable chunk ownership per phase. Two parts
of the source proposal are not: Tess has no persistent region-to-worker
ownership concept, and per-chunk memory-footprint and bandwidth instrumentation
does not exist outside the controlled-hardware counter pass, which is never a
published timing source. Run the static version only, and judge it on lower
boundary traffic or better useful-work scaling — never on balance itself,
which is what a balancing algorithm optimizes and therefore proves nothing.
Incremental repartitioning opens only on a strong static result.

### E5: portable locality precursor

The fragment of the NUMA program that is useful without any NUMA hardware, and
the cheapest experiment here. In scope: periodic statistics aggregation
against continuously shared counters; false-sharing avoidance; worker-to-data
locality; topology discovery where a platform exposes it; and sharded queues
or arenas where those are independently justified on the existing pool. The
codebase already isolates contended worker-pool state behind 128-byte
alignment, and the budgeted-progress design already forbids contended hot-loop
atomics in measured paths, so the precedent and the measurement discipline
both exist.

Hierarchical stealing belongs here too, and is testable today. The M3 exposes
real cache-domain boundaries — six CPUs per L2 on the performance cluster and
four on the efficiency cluster — so an escalation of own queue, then same
cache domain, then a farther one can be measured against random or global
stealing without any NUMA hardware. If it wins on ordinary multicore, it wins
on its own merits and also becomes the portable half of the deferred track's
stealing work.

Admit only changes independently justified on ordinary multicore systems.
Sharded queues, counters, free lists, or scratch state qualify when they
measurably reduce contention or cache-line bouncing; topology and capability
discovery qualifies when it degrades gracefully where a platform exposes
nothing.

Runs on both controlled platforms, needs no rented hardware, and is
independent of every other experiment here. One constraint is absolute: E5
results are never reported as NUMA results. Sharding a queue on a single-
domain machine says nothing about a multi-domain one, and conflating the two
is how a deferred program smuggles itself back in.

## Deferred past 1.0

**The NUMA program (2A-2K).** Secondary for now rather than discarded: it is
reclassified as a hardware-specialized research track serving a best-effort
server tier, with its own acceptance criteria separate from Tess's normal
cross-hardware portability gate, and with its standing revisited after 1.0.
That separation is forced rather than chosen. Its own precondition — genuine
multi-domain hardware — excludes both controlled platforms, so roughly
eleven of its sub-experiments cannot produce one valid data point on the
current fleet, and the cross-hardware rule can never be satisfied for a
NUMA-motivated change.
The codebase contains no affinity, topology, or placement substrate.

### Server hardware is a best-effort tier, not a third supported platform

Multi-domain server hardware is a **best-effort target** and stays secondary
for now. It does not join the two supported platforms, and the distinction is
deliberate rather than a placeholder for eventual parity:

- **No shared promises.** It is outside the cross-hardware decision rule, has
  no calibrated CI ceilings, and gains no support-policy commitment. Evidence
  from it is labelled single-platform and is never published as a portable
  claim.
- **It may not pessimize the supported platforms.** Any change motivated by
  server hardware must be neutral or better on the M3 and the SteamOS-class
  device under the ordinary paired rule. That is a testable gate, not a
  sentiment, and it is the one the tier is most likely to fail.
- **It may not break them.** No platform-specific path reaches a stable
  header; capability queries degrade to a no-op where a platform exposes
  nothing, and portable behavior stays identical.
- **Determinism is not negotiable for it.** Placement and affinity must leave
  simulation-visible outcomes unchanged. A best-effort tier does not buy an
  exemption from a documented decision.
- **Single-platform evidence is admissible here, and only here.** Because the
  cross-hardware rule cannot apply, the tier carries its own hardware-specific
  success decision.

This is a pre-1.0 stance. After 1.0 the tier may well be promoted, and that
promotion is its own decision with its own calibration and support-policy
consequences — not a drift.

### The cheaper-evidence gate is closer to met than it looks

An earlier revision of this document assumed no locality evidence existed
short of new spend. That was wrong, and the correction matters: the published
scaling campaign already ran on the four-domain metal instance, and it records
a wall. Beyond roughly 24 workers its measurements are too noisy to publish at
all, and two-worker results are withheld.

That ceiling coincides exactly with a topology boundary. The machine has 96
physical cores across four NUMA domains — 24 physical cores per domain — and
the campaign pinned one worker per physical core. The publishable ceiling and
the first domain crossing are the same point.

Treat that as a hypothesis worth testing, not a finding. The data cannot yet
distinguish the mechanism, and two features of the campaign actively
complicate the obvious reading. It ran under `numactl --interleave=all`, which
flattens placement by design — every worker was already mostly remote at one
worker, so first-touch locality is *not* the natural explanation for something
that changes at 24. Bandwidth or fabric saturation and coherence traffic fit
the shape better. And the L3 is per-socket, so on this SKU a cache-domain
boundary plausibly coincides with the NUMA boundary; this campaign cannot
separate them. Prior work also traced an earlier two-worker anomaly to
dispatcher CPU starvation rather than topology, which is a standing reminder
that a boundary-shaped cliff need not be a boundary-caused one.

The consequence is practical. The next step is not a purchase: re-read the
retained campaign artifacts, then run the deliberately-bad-placement control
and a non-interleaved control on that instance. Only if those reproduce and
localize the effect does anything further deserve funding, and only then is a
third calibrated platform worth discussing.

### Topology is not a node number

Do not design around `struct NumaNode { int id; };`. Real machines carry
several NUMA regions inside one socket, several cache and CCD relationships
inside one NUMA region, asymmetric and multiple remote distances, CPU-less
memory nodes, CXL and HBM tiers, and devices with their own topology
affinity. The shape that survives that is a weighted cost model over compute
domains, memory targets, and I/O targets, with the cost between them
**measured** rather than read from a firmware distance number.

Nothing about that is a reason to build a production abstraction now. It is a
reason to ensure any experiment or small internal topology interface does not
bake in a local-versus-remote binary that forecloses it later. `hwloc` is a
reasonable discovery candidate beneath Tess's own abstraction; no `hwloc`,
`libnuma`, or Win32 type appears in a public Tess API.

### Ordered stages, cheapest kill-check first

The source sequence began with exhaustive hardware characterization. That is
backwards by this document's governing rule and is reordered here: establish
that a problem exists before paying to measure the machine in full.

1. **Deliberately bad placement, as a positive control.** Force world pages
   onto one memory node while spreading workers across several. This answers
   three questions at once: whether the Tess workload is NUMA-sensitive on
   that machine at all, whether the profiling counters actually detect the
   pathology, and what upper bound on gain exists. If deliberately terrible
   placement barely moves useful Tess work, the rest of this track is
   deprioritized and the money is not spent.
2. **Hardware characterization.** Only now record the full topology and
   configuration, and measure — rather than trust — dependent-load latency
   and streaming read, write, and copy bandwidth for each meaningful compute
   domain by memory target. Preserve the directional matrices; they are what
   a cost-aware scheduler would eventually consume.
3. **Scaling and locality cliffs.** Benchmark the unmodified workload densely
   around each boundary rather than at powers of two: cache and CCD, NUMA
   region, socket, and the physical-core to SMT transition. Ask whether the
   first worker placed outside a locality domain reduces marginal throughput,
   and whether N workers can beat N+1. Strong scaling first; weak scaling —
   more domains against a proportionally larger world — is where spatial
   ownership should matter most.
4. **Affinity only.** Pin workers by topology without touching memory
   placement, isolating execution placement from data placement. A pinned
   thread is not a thread with local data; those are separate questions and
   must not be reported as one.
5. **First-touch placement.** Test correct placement before any custom
   allocator. The trap is concrete: `std::vector<Tile> tiles(count);`
   value-initializes on the constructing thread, so the pages are physically
   placed before any later parallel traversal — that traversal is not
   first touch. Reserve address space, assign regions to home domains, let
   domain-affine workers construct their own regions, then **verify actual
   page distribution** rather than assuming affinity produced it.
6. **Domain-local arenas.** Placement works at page granularity, so do not
   NUMA-allocate individual objects: map a large backing region per domain and
   suballocate chunk state, fields, scratch, scheduler nodes, and caches from
   it, which also stops differently-owned objects from sharing a page. One
   constraint applies here that does not apply on a generic codebase — virtual
   declarations appear only in the experimental maintenance layer, so a
   `std::pmr::memory_resource`-shaped arena stays a prototype under
   `tess::experimental` and cannot reach a stable surface without its own
   decision.
7. **Sharded scheduler and shared state.** Treat global queues, completion
   counters, statistics, dirty counters, free lists, and scheduler metadata as
   coherence hazards independent of remote DRAM, preferring per-domain state
   with occasional aggregation where it measurably helps. A lock-free global
   structure is not thereby NUMA-friendly; cache-line ownership still bounces.
8. **Separate remote memory from coherence.** A cliff has at least two
   mechanisms — remote DRAM access, and cache-line ownership traffic such as
   HITM and false sharing — and they need different fixes. Profile both, with
   `perf c2c` or an architectural equivalent, and normalize against output:
   cross-domain bytes per useful operation, remote HITM per useful operation.
   Never read an allocation counter such as `numa_miss` as if it counted
   runtime remote accesses.
9. **Owner-computes routing.** Send work toward the data it touches, costing
   multi-domain operations by their actual working set rather than a nominal
   owner. The queued-operation architecture may let cross-domain work resemble
   message passing to the owning domain rather than uncontrolled remote
   shared-memory access.
10. **Hierarchical stealing.** Replace local-versus-remote with topology-aware
    escalation, each level carrying an estimated cost. This is where E5's
    portable result feeds in.
11. **Selective replication.** Test per-domain copies of small, heavily read,
    rarely written structures — coarse connectivity, the hierarchical
    navigation graph, portal and static terrain metadata, small lookup data —
    measuring traffic avoided against memory and update cost. Do not replicate
    large mutable state.
12. **Locality against load balance.** Only now ask whether strict locality
    over-constrains execution. Executing remotely once may legitimately beat
    waiting for a local worker.
13. **Discrepancy correction, last.** Accumulated imbalance enters only as a
    pressure term on top of locality and execution cost, and only to answer
    one question: does imbalance information improve application-visible
    outcomes over an already competent locality-aware scheduler? If not,
    discard it. Discrepancy remains diagnostic here exactly as it is
    everywhere else in this document.
14. **Migration, with a much higher threshold than remote execution.** Remote
    execution costs one operation; moving ownership changes future locality
    and can move substantial memory. Use hysteresis — tolerate brief hotspot
    movement, evaluate migration only for sustained movement — and require
    evidence that sustained ownership change actually occurs in representative
    workloads before implementing page migration.

### Policy is not always "local"

Local-node placement suits mutable spatial and chunk state, domain scratch,
and scheduler structures. Small static shared data may be better replicated. A
large globally streamed array, or shared bandwidth-dominated data, may be
better interleaved: using several memory controllers can win even though it
raises nominally remote traffic. Future tiered memory needs a tier-aware
policy rather than a local-or-remote one.

### Confounders that must be controlled and recorded

Automatic kernel NUMA balancing will silently repair or fight Tess placement,
so benchmark both Tess-unaware and Tess-managed against it on and off rather
than leaving it uncontrolled. Page size is its own experiment — normal, THP,
and explicit huge pages change TLB behavior, placement and migration
granularity, first-touch behavior, and fragmentation — and must not be
changed midway through another comparison. Firmware topology modes such as NPS
and SNC turn one physical CPU into materially different machines and are
treated as distinct hardware. Virtualized vNUMA results are labeled as such
and never mixed with bare metal, which is what serious conclusions require.

Measurement discipline otherwise defers to the rules already in this document
and to the existing benchmark and calibration documents; that includes paired
interleaved runs, retained raw per-run samples, medians rather than best-of-N,
and separating the timing pass from the invasive profiling pass. The
NUMA-specific additions are frequency and thermal monitoring, and enough
topology and system metadata in every artifact to reinterpret it later.

### Acceptance

The north star stays application-visible: useful Tess work completed subject
to its latency, deadline, and budget contract, with strong and weak scaling
efficiency, tail latency, and backlog stability alongside it. Placement,
traffic, HITM, per-domain bandwidth, queue wait, and steals by topology
distance are explanatory only.

An optimization is never accepted because it raised local-access percentage.
That is the same circularity this document rejects elsewhere: local-access
share is what a locality optimizer optimizes. An optimization may legitimately
increase remote accesses if it buys enough bandwidth or utilization to
complete more useful work. Report the stage progression rather than one
before-and-after number — if nearly all the benefit came from fixing
first-touch, that is the finding, and it does not belong to the later
machinery.

The governing statement for the whole track:

```text
hardware topology
  -> measured compute/memory/IO costs
  -> spatial and data ownership
  -> memory placement
  -> domain-local execution and state
  -> owner-computes
  -> topology-aware stealing
  -> locality against load balance
  -> discrepancy correction
```

NUMA optimization is not minimizing remote memory. It is maximizing useful
Tess work under its latency and budget contract by making compute, cache,
memory, and interconnect work together.

### Portability constraints

There is no standard C++ NUMA API, so every platform detail stays behind one
small internal hardware-topology abstraction, and no OS-specific type appears
in a public Tess API. Linux would use libnuma and pthread affinity, dropping
to `mbind`, `set_mempolicy`, or `move_pages` only where needed, and retaining
page-distribution evidence from `/proc/<pid>/numa_maps` or equivalent
tooling; Windows would use its native topology, CPU-set, and
`VirtualAllocExNuma`-style calls; macOS offers no comparable explicit
memory-placement control and must degrade gracefully rather than fail.
Discovering a placement operation is not evidence the host supports it.
Capability queries must let every NUMA experiment compile and run as a no-op
or fallback on unsupported platforms, which is also what keeps such work out
of the header-only library's portable surface.

**CPU/GPU allocation.** The premise is not that the GPU layer is missing —
`include/tess/gpu/webgpu_backend.h` is a real transport with upload, dispatch,
readback, and device-loss handling. The premise is that the set of operations
having both a CPU and a GPU implementation is **empty**: pipelines and shader
meaning are provider responsibilities by design, and simulation code must
validate or recompute any gameplay-exact answer on the CPU. Device selection
for authoritative work therefore contradicts a standing decision rather than
merely lacking code, and with no operation for which CPU-versus-GPU execution
is a policy choice, the experiment has no substrate to run on.

A secondary note, stated precisely because an earlier draft of this document
overstated it: both controlled machines are unified-memory, which reduces but
does not eliminate placement cost. Submission, synchronization, coherency, and
GPU-side residency all still cost, and the staging copies remain. The reason
to defer is the empty operation set, not the memory architecture.

The prerequisite is at least one real compute-shader implementation of a Tess
operation with a CPU oracle, where runtime placement is genuinely optional —
a feature program, not an experiment.

## Dropped

**Discrepancy-aware integerization.** Spawn counts, jobs per region,
resources, and density-to-entity conversion sit outside Tess's authority
boundary by existing decision: callers own entity storage, job logic, and
content rules, and the maintenance contract explicitly excludes that
territory. This is recorded the way the execution plan records non-experiments
— excluded by an existing boundary, with no speculative fragment. Largest-
remainder rounding needs no research prototype should a concrete use case
appear later.

## The shared primitive, and what would falsify it

The program hypothesizes a reusable rule: `deficit[d] = actual[d] -
desired[d]`, each action supplying a sparse `delta`, selection minimizing
`immediate_cost + locality_cost + lambda * penalty(deficit + delta)`.

As stated it is unfalsifiable. A free `lambda`, an unspecified penalty family,
unspecified per-dimension weights, and per-experiment re-tuning can fit any
outcome, and "reusable" is undefined. Most of the proposed dimensions — NUMA
capacity, bandwidth, remote traffic, GPU load — cannot be populated on
available hardware at all, so the multi-dimensional claim is untestable and
only the scalar and small-vector service-fairness case is in scope.

To make it testable, fix in advance: the penalty family; the procedure that
selects `lambda`, chosen on a held-out workload and then frozen; and the reuse
criterion — one implementation, a declared maximum number of tuned parameters,
beating the best simple baseline on the pre-registered independent endpoint by
more than the paired noise threshold on both controlled platforms, in at least
two experiments. Pre-commit that failure falsifies the primitive rather than
motivating a new dimension. No public abstraction is designed before that
criterion is met.

Surviving prototypes are then ranked on useful-work improvement, tail-latency
and deadline improvement, reduction in pathological imbalance, runtime
overhead, implementation complexity, hardware portability, generality across
Tess operations, and compatibility with the existing architecture. The last
four decide adoption at least as often as the first four, which is why the
reuse criterion above counts tuned parameters rather than only wins.

## Hardware matrix

| Experiment | M3 | SteamOS | Metal | Note |
| --- | --- | --- | --- | --- |
| E0 | No | No | No | Offline replay only |
| E1 | Required | Required | No | Both legs for a perf claim |
| E2 | Required | Required | No | MNT-3 makes the APU decisive |
| E3 | Required | Required | No | Nanosecond overhead budget |
| E4 | Conditional | Conditional | No | Required once perf decides |
| E5 | Required | Required | No | Cheap and independent |
| NUMA | Excluded | Excluded | Needed | Needs a third platform |
| CPU/GPU | Excluded | Excluded | No | Needs a real kernel first |

"Conditional" never permits a performance-only rejection from one platform.

## Dependency summary

```text
E0 (offline replay kill-check)
    |
    +-- headroom? --no--> discard the selection-policy program
    |
   yes
    |
   E1 (service selection, absorbs fractional fidelity)
    |
    +--> E2 (invalidation) -- own pathology gate first, else killed
    +--> E3 (residency cache) -- competing working sets first, else killed
    +--> E4 (static partitioning)

E5 (portable locality precursor) — independent, anytime;
    also the cheaper evidence the deferred NUMA track is gated on

NUMA, CPU/GPU — deferred past 1.0, each behind its own prerequisite
```

## Provenance

The source program was drafted separately and reviewed once by an independent
model before this document was written. The review is the origin of the
circular-metric finding, the ill-defined incumbent arm, the empty CPU/GPU
implementation set, the MNT-3 prior, the redundancy of the fractional-fidelity
mechanism, the stage-0 replay kill-check, the wrong-signal control, and the
missing stride, deficit-round-robin, and modern cache baselines. Hardware
topology, substrate mapping, and the cross-hardware-rule consequence were
verified directly against the repository and the machines.

Every review finding was adopted; none was rejected. Its citations were
spot-checked against the cited files before adoption rather than taken on
trust.

A second review round accepted that restructuring and refined it. From it come
the governing pathology-first rule, the broadened independent-endpoint list
and its generalization beyond scheduling, the pathology gate that now opens
E2, the competing-working-sets precondition on E3, E4's endpoint correction,
E5's widening into a portable locality precursor together with its prohibition
on reporting NUMA results, the reclassification of NUMA as a
hardware-specialized track behind a cheaper-evidence gate, and the correction
of an overstatement in an earlier draft of this document, which had claimed
unified memory eliminates GPU placement cost.

A third round supplied the deferred track's detail: the weighted
compute/memory/IO topology model in place of a node number, the
deliberately-bad-placement positive control, the first-touch trap in
single-threaded container construction, the separation of remote-DRAM cost
from coherence traffic, the warning against reading allocation counters as
runtime access counts, non-local placement policies for bandwidth-dominated
data, the automatic-balancing, page-size, topology-mode, and virtualization
confounders, and hierarchical stealing as portable work.

Three changes were made to that round rather than transcribing it. Its
sequence opened with exhaustive hardware characterization; that is reordered
behind the positive control, because measuring a machine in full before
establishing that a problem exists inverts this document's governing rule and
spends money first. Its measurement-discipline and result-presentation
sections largely restated rules the repository already owns, so they are
replaced by a deferral to those rules plus the genuinely NUMA-specific
additions; specifying chart layouts for a track gated on whether a pathology
exists is the speculative machinery the prototype rules warn against. And its
`std::pmr` arena shape acquired a constraint it did not carry: virtual
declarations appear only in the experimental maintenance layer, so that
prototype cannot reach a stable surface without its own decision.

A maintainer decision then set multi-domain server hardware as a best-effort
target, secondary before 1.0 and open to promotion after it, constrained so it
cannot pessimize or destabilize the two supported platforms. The same exchange
surfaced evidence this document had overlooked: the published scaling campaign
already ran on the four-domain instance and records an unpublishable-noise
wall beyond roughly 24 workers, which is exactly one NUMA domain's worth of
physical cores at the campaign's one-worker-per-core pinning. That
observation, and the reasons it is a hypothesis rather than a finding — the
interleaved memory policy, the coinciding per-socket cache boundary, and a
prior two-worker anomaly that proved to be dispatcher starvation — replaced
this document's earlier assumption that no locality evidence existed short of
new spend.
