---
description: >-
  Experimental native graph and grid composition, bounded exact routes,
  retained snapshots and shared downstream-cost navigation guidance.
---

# Composable navigation

**Experimental:** include `tess/navigation/query.h` for exact graph composition
and `tess/navigation/grid.h` for native grid adapters. Existing standalone grid
APIs and stable aggregate membership are unchanged. This surface is undergoing
qualification and is not promoted to the stable contract.

## Representations and physical meaning

`NavigationDomain` is an immutable indexed directed-transition interface.
`NavigationGraph` owns explicit edges. `NavigationGrid` retains native grid
storage and movement classes; its default `NavigationRegularEdges` policy
permits regular transitions, with an optional owned host veto. A
`NavigationLocation` combines a domain object's non-recycled lifetime identity
within one linked image with a local node key.
These identities are not portable save-game IDs or physical support IDs.
`NavigationLocalEdge` retains original forward endpoints even when enumerated
in reverse. Local edge identity is `(domain, original from node, id)`;
explicit graph IDs are additionally unique within their graph.

`NavigationConnection` connects checked endpoint pairs with its own ID, cost,
availability and opaque host key. `NavigationTransition` retains the chosen
local edge or connection. Multiple pairs represent a wide opening without
collapsing its geometry. A connection can have zero cost; crossing a
representation seam need not trigger a stop or action. The host validates
physical passage, transforms, supports and eligibility. Overlapping coarse
representations must not bypass obstacles in authoritative fine detail.

`NavigationSnapshot` retains domain ownership and indexes connections during
explicit preparation. Preparation is separate from budgeted querying. Domain
and policy data, including data reachable through pointers, must remain frozen
throughout the snapshot lifetime. `shared_ptr<const T>` alone does not prevent
writes through another alias. All access needs external synchronization.

`NavigationSnapshot::diagnostic()` retains the first preparation failure as a
`NavigationPreparationDiagnostic`: its `NavigationPreparationError` reason,
input index and domain or connection ID. Domains are checked before connections
in supplied order. Missing source/destination membership, reserved costs and
duplicate attachment IDs have distinct reasons. This diagnostic explains
structural rejection; physical passage remains host-validated.

## Costs and proof

`NavigationQuery` uses Dijkstra without a geometric heuristic. Costs share a
caller-chosen nonnegative integer convention; UINT64_MAX is reserved, leaving
zero through UINT64_MAX-1 representable. Zero-cost cycles terminate through
settlement and strict improvement. Parallel edges retain their chosen identity.
Grid scale and diagonal multipliers must map into the same convention.
Summed overflow candidates cannot beat finite alternatives. Unresolved overflow
endpoints consume discovered-label capacity; shared guidance refuses overflow
only if an endpoint still has no finite alternative when search exhausts.

`NavigationOutcome` separates found products, no route, indeterminate topology,
invalid input, arithmetic overflow and capacity refusal. Completed negative
outcomes are readable results with `AsyncResultState::Ready`; cancellation,
supersession and stale input are separate lifecycle states with no readable
query result. `NavigationAvailability` distinguishes blocked, unavailable,
invalid and overflow candidates from legal transitions.

`NavigationProof::ExactSnapshot` means exact for the represented input graph.
It does not claim geometric shortest motion, future congestion prediction,
resource reservation or accepted movement. Incomplete snapshots conservatively
produce `FeasibleOnly` paths; exhausted forward search reports `Indeterminate`
instead of conclusive `NoRoute`. Guidance with no usable goal likewise reports
`Indeterminate` when goals are unavailable, or `NoRoute` when they are blocked
and the snapshot is complete. The sparse grid adapter treats its snapshot as
incomplete unless every chunk is resident. Missing detail may contain a cheaper
alternative.

## Guidance and freshness

A one-goal or `NavigationGoal` vector query computes shared reverse guidance.
Goals may include nonzero boundary costs. `NavigationGuidance` stores complete
remaining cost and the original forward successor; costs are not reset to zero
at exits. Successors point to earlier-settled states, so extraction terminates
even across zero-cost cycles and repeated domain crossings. Query labels cover
reachable states rather than a dense array over combined coordinate extents.

`NavigationResult` owns its route or guidance and retains the input snapshot.
`current_for` requires identical retained snapshot identity. Every profile,
content, topology or residency change requires a new snapshot, including
shortcut additions outside an old route and changes affecting cached negative
answers. Old results remain readable as historical data; they do not authorize
execution. `require_snapshot` prevents a query publishing after replacement.
The host must call it when changing inputs and check freshness at use time.

## Work and storage

`NavigationLimits` caps discovered labels, queued frontier entries and retained
route steps. Refusal publishes no partial successful route or guidance.
`advance(AsyncWorkBudget)` accounts initialization, one seed, one frontier pop,
one candidate, reconstruction append, reversal swap, guidance-label export and
publication. Ordered lookup and heap work are logarithmically bounded by the
explicit capacities and prepared snapshot index sizes. High-degree enumeration
is resumed between individual candidates. Array capacities are reserved once;
query steps do not relocate existing array elements.

Allocator work and domain callbacks are outside the deterministic item bound;
callbacks must not hide unbounded enumeration. Native sparse page-directory
lookups can scan collision clusters; the fixed geometric candidate count does
not make those storage probes constant-time. Snapshot construction and query
destruction are explicit work outside `advance`. The contract is not a hard
time guarantee. Allocation/callback exceptions propagate; discard a query after
an exception. Exception-free builds retain checked invalid-input/capacity paths
but do not promise recovery from allocation failure.

The queue callback publishes a shared pointer in constant work. Its output
slot must be empty or already own this exact result through the same control
block. Other slots return `Failed` with zero work and leave query/output
unchanged; release old outputs explicitly outside the budgeted callback. This
also rejects aliasing slots that would hide arbitrary destructor work. Keep the
nonmovable query alive until its `ResumableWorkQueue` ticket is retired. The
queue retains its own FIFO, summary-scan and non-owning-context constraints;
its whole advance is not covered by the navigation kernel bound. Flow accounting
can attach to the existing queue. `work_items` describes deterministic query
work; `payload_bytes` describes logical result payload, excluding allocator
metadata and shared snapshot storage, not whole-process memory peaks.

`navigation_route` is a synchronous driver over the same continuation. Hosts
retain ownership of scheduling, physical publication, admission, occupancy,
steering and accepted motion. The library creates no hidden threads.

## Executable example

`examples/composable_navigation.cc` connects a
graph, a corridor grid and a second grid with a different schema and scale. It
checks both travel directions and budget-one shared guidance, including through
the installed package with RTTI disabled. Run `tess_composable_navigation` from
a developer build. Qualification outcomes are recorded separately from this
example's narrow synthetic claims.

## Performance qualification

The advisory `lab/navigation/` workloads include snapshot preparation, query
scratch and retained output. They establish correctness and deterministic work
counts; they do not yet have calibrated paired performance thresholds. The
source classifier therefore marks this experimental directory unrepresented
in paired sentinels. Changes to shared storage, path or build infrastructure
retain their existing performance selection. The `scale_*` cells compose two
always-resident grids, one fully resident sparse grid and a directed graph,
then extract complete ordered transitions for the same requests from repeated
routes and from shared guidance at 32, 256 and 1024 requests. Both are
checked after timing against an independent materialized oracle for legality,
termination and equal per-request optimal cost; they often follow different
equal-cost paths. Separate cells expose setup, query, extraction, warm reuse,
snapshot replacement and bounded admission refusal. Before promotion, qualify
representative graph/grid compositions, query distributions and shared-goal
reuse on controlled hardware, then select a representative sentinel and remove
that directory exclusion together. An unrelated legacy path sentinel cannot
measure this implementation.
