# Pathfinding prototype survey

Status: **Point-in-time research brief** (2026-08-18). This document preserves
candidate discovery, ordering, and investigation gates. It is not current
architecture, a roadmap commitment, an accepted design, or permission to add
public API. Active work belongs in an issue and pull request; completed
performance experiments belong in the
[optimization log](optimization-log.md).

The [roadmap](../roadmap.md) remains the authority for released, release-gated,
deferred, and out-of-scope work. The maintained
[path](../architecture/path.md) and
[simulation](../architecture/simulation.md) notes remain the authority for
current behavior. They state that request-count budgets do not bound the
expansions or wall time of one synchronous A*. The
[local movement screening study](local-movement-resolution.md) records the
separate layering conclusion that target fungibility remains caller knowledge.

## How to use this brief

When a candidate is selected, open one bounded prototype issue that records:

- the hypothesis and smallest private implementation capable of testing it;
- the baseline, interleaved control, fixtures, and independent correctness
  oracle;
- pre-registered metrics and a go/no-go threshold derived from measurement
  noise rather than from the result;
- allocation, memory, determinism, versioning, invalidation, and ownership
  constraints;
- the evidence artifact location, stop condition, and condition for later
  reconsideration; and
- pinned source revisions and license compatibility before code or fixtures
  are copied.

Keep the mechanism internal until the evidence justifies more authority. Once
the run finishes, add one fragment under `optimization-log.d/` recording its
method, result, decision, limitations, and reconsideration condition. Accepted
implementation work must update maintained architecture; add a design-decision
fragment only when the choice materially changes design intent, and add a
release changelog fragment only for a user-visible landed change.

Do not create speculative optimization-log fragments, TDDs, or public
abstractions merely to hold a candidate. A separate problem brief or TDD is
warranted only if investigation selects a durable contract such as resumable
search state, uncertain execution semantics, temporal multi-agent planning, or
radius-aware topology.

## Recommended investigation order

The order below ranks experiments, not product promises. None changes the
current release gates.

1. **Portal-seam lookup/index.** Require exact result equivalence, bounded
   memory, correct topology/version invalidation, and a material end-to-end win
   against an interleaved control.
2. **PIBT hindrance tie-break.** Preserve route-attachment rank as the primary
   oracle. Require determinism and terminal parity plus a quality win on dense
   fixtures without unacceptable tick or allocation cost.
3. **Resumable and cancellable single-query A*.** Use caller-owned state and
   one expansion as the work unit. Define deterministic pause/resume plus
   cancellation, staleness, content-version, and lifetime behavior.
4. **Gated JPS specialization.** Admit only resident dense regular 2D,
   uniform-cost movement with no custom transitions. Reject if median workloads
   regress or the capability gate becomes complicated.
5. **Execution-delay and uncertainty harness.** Inject delayed action
   completion and measure collisions, deadlocks, throughput, and recovery
   before proposing an execution contract.
6. **Fungible-goal target swapping.** Keep this a caller-owned assignment
   helper with explicit fungibility. Identity-sensitive tasks and accounting
   must remain unchanged.
7. **Conflict-component temporal escalation.** Reopen only after a reproducible
   reciprocal-conflict fixture defeats seeded routing and PIBT, then plan only
   the affected component.
8. **Derived clearance/radius product.** Treat this as post-1.0 investigation
   because capability-specific topology affects caching, invalidation, and
   movement-class semantics.

## First prototype: portal-seam index

The current portal-tick work identifies `detail::best_chunk_portal` as a
prominent cost centre. The remeasurement prepared in
[PR #207](https://github.com/kindjie/tess/pull/207) reproduces the call-level
redundancy premise but leaves the removable-cost ceiling unproven. It explicitly
leaves a seam-local index unmeasured. Treat that PR as pending evidence; before
implementation, require its result to land or reproduce the measurement on the
selected base commit.

**Hypothesis:** a compact seam-local lookup can remove repeated scoring work
that call-level memoization cannot, without adopting a quadtree or navmesh
storage model.

**Controlled change:** index only the portal candidates needed to answer a
`(from_chunk, to_chunk)` seam query. Keep route selection, chunk layout, and
public interfaces unchanged. Define construction and invalidation at the same
topology/version boundary as the authoritative seam data.

**Oracle and measurements:** compare every indexed result with the existing
scan, including empty, tied, edited, sparse-residency, diagonal, hex, and
provider-composed cases. Measure end-to-end portal-tick time, index build and
invalidation cost, resident bytes, allocations, and the selected call site's
profile share. Interleave control and candidate runs and preserve the raw
artifacts.

**Decision:** accept only if exact equivalence and lifecycle gates pass and the
end-to-end change exceeds the pre-registered noise-adjusted threshold. Reject
or defer on a local microbenchmark win that disappears at the tick boundary.

## Second prototype: PIBT hindrance tie-break

The [local movement screening study](local-movement-resolution.md) found that
candidate ranking alone did not replace priority inheritance, that exact route
guidance mattered, and that target swapping belonged above the movement layer.
Hindrance is therefore a secondary ordering experiment inside the existing
PIBT authority boundary, not a new planner.

**Hypothesis:** among candidates with equal route-attachment rank, estimated
interference with nearby agents improves solution cost or throughput more than
deterministic enumeration alone.

**Controlled change:** preserve route-attachment rank as the primary oracle.
Apply hindrance only to equal-rank candidates, before the existing deterministic
enumeration fallback. Do not add regret, goal reassignment, temporal search, or
a public policy until hindrance itself passes.

**Oracle and measurements:** exercise dense warehouse, ring, colony, random,
and adversarial fixtures. Compare terminal outcomes, final occupancy, route
validity, deterministic replay, allocations, tick cost, throughput, solution
cost, blocked retries, and deadlock incidence. Include the current PIBT policy
and deterministic enumeration as controls.

**Decision:** accept only with terminal parity and determinism, no warm-path
allocation regression, and a pre-registered quality improvement that survives
the fixture matrix at acceptable tick cost. Try regret only after hindrance
wins and only as a separate experiment.

## Later candidates and authority gates

- **Resumable A*.** Existing campaign evidence already identifies single-query
  pathfinding as a possible indivisible quantum. Start with internal unit-cost
  and weighted A* parity. A production continuation proposal must satisfy the
  evidence requirements in
  [budgeted-progress benchmarks](budgeted-progress-benchmarks.md), including
  persistent state, cancellation, invalidation, deterministic ordering, and a
  contiguous-versus-resumed oracle.
- **JPS.** Restrict symmetry pruning to the narrow capability intersection it
  can prove equivalent for. Compare open, wall-gap, maze, and rubble maps with
  Tess's current direct and prechecked paths. Complexity in the capability gate
  is itself a rejection signal.
- **Execution uncertainty.** Build a harness before a mechanism. Compare
  synchronous plans, request/move/release states, and action-dependency edges;
  the result should decide whether Tess needs a new execution contract.
- **Fungible goals.** Prototype continuous target reassignment only where the
  caller explicitly supplies an anonymous goal set. Never infer fungibility
  from individually assigned goals.
- **Temporal escalation.** Do not reopen a global WHCA-, CBS-, or LaCAM-class
  solver from generic literature evidence. Require a Tess fixture that defeats
  current seeded routing and PIBT, then isolate the conflict component and use
  a tiny-instance optimal solver only as an oracle.
- **Clearance/radius.** Derive capability-specific products from existing cells
  and chunks if a concrete post-1.0 use case appears. Do not replace fixed chunk
  storage with a quadtree or navmesh as part of that investigation.

## Source map

These primary papers motivate experiments or provide external oracles; none is
a dependency recommendation. Select and pin any implementation reference only
inside the bounded prototype that needs it, after license review.

- Portal hierarchy and indexing:
  [ENLSVG](https://arxiv.org/abs/1702.01524).
- PIBT tie-breaking:
  [Lightweight and Effective Preference Construction in PIBT](https://ojs.aaai.org/index.php/SOCS/article/view/35982).
- JPS:
  [Online Graph Pruning for Pathfinding on Grid Maps](https://ojs.aaai.org/index.php/AAAI/article/view/7994)
  and
  [Improving Jump Point Search](https://ojs.aaai.org/index.php/ICAPS/article/view/13633).
- Execution uncertainty:
  [Time-Independent Planning for Multiple Moving Agents](https://ojs.aaai.org/index.php/AAAI/article/view/17347),
  [Agile Multi-Agent Path Finding](https://www.nature.com/articles/s44182-026-00083-2),
  and [SMART](https://arxiv.org/abs/2503.04798).
- Fungible targets:
  [Solving Simultaneous Target Assignment and Path Planning Efficiently with
  Time-Independent Execution](https://arxiv.org/abs/2109.04264).
- Conflict-local temporal planning and optimal oracles:
  [Cooperative A*](https://ojs.aaai.org/index.php/AIIDE/article/view/18726),
  [Conflict-Based Search](https://tzin.bgu.ac.il/~felner/2015/CBSjur.pdf),
  [iterative refinement](https://arxiv.org/abs/2102.12331), and
  [branch-cut-and-price MAPF](https://www.ijcai.org/proceedings/2019/179).
- Clearance and continuous duration:
  [AA-SIPP-m](https://ojs.aaai.org/index.php/ICAPS/article/view/13856).
- Broader references with deliberately limited transfer:
  [Lazy Theta*](https://ojs.aaai.org/index.php/AAAI/article/view/7566),
  [LaCAM*](https://arxiv.org/abs/2305.03632), and
  [large-scale LaCAM](https://ifaamas.csc.liv.ac.uk/Proceedings/aamas2024/pdfs/p1501.pdf).

## Explicit deferrals

- Full LaCAM, CBS, ICTS, or branch-cut-and-price solvers in Tess core.
- Neural routing guidance and training or inference dependencies.
- Navmesh generation, RRT, or continuous crowd steering.
- Any-angle routes before line-of-sight, timing, smoothing, and route-validity
  contracts exist.
- Another general flow-field implementation.
- Dynamic congestion costs in the colony scenario without evidence that
  overturns the prior incomplete-arrival regression.
- A public abstraction for any candidate before the private prototype proves
  value.
