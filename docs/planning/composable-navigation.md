# Composable navigation implementation design

Status: experimental additive implementation under evaluation; not release
qualification.
Lifecycle: implementation intent; current contracts live in
`docs/architecture/navigation.md` and public header comments.

## Contract and alternatives

Independent explicit graphs and native grid worlds compose through checked,
directed attachments. Domain identities identify object lifetimes, not geometry
or C++ types. Immutable retained snapshots bind domains, connections and profile
policy. Replacing any input creates a new snapshot; exact retained products are
current only for that identical snapshot. This deliberately conservative rule
invalidates negative results and shortcuts outside the selected route too.

Use a distinct experimental navigation header family. Existing grid entry
points, aggregates and umbrella membership stay unchanged. Promotion requires
independent correctness, package and downstream qualification before stable
classification; a local example alone does not authorize promotion.

A small runtime domain interface has indexed forward/reverse edge enumeration,
endpoint membership and availability. Explicit graph storage owns its edges;
grid adapters retain an immutable world lease and generate regular movement
candidates. Arbitrary legacy push providers are not accepted as bounded graph
cursors. Hosts supply explicit graph edges for special physical transitions.
A host eligibility predicate may veto grid transitions; standable adjacent
cells alone do not establish swept passage.

Use exact Dijkstra with nonnegative uint64 costs and zero-cost connections.
Reserve UINT64_MAX for unreachable; addition reaching it reports overflow.
Grid costs preserve the existing movement class and diagonal multipliers,
then apply an explicit integer scale to the common query unit. No geometric
heuristic is assumed across heterogeneous representations. Parallel edges and
opaque caller traversal keys survive output. Wide openings are multiple exact
attachments, each retaining its endpoints; a seam has no mandatory action cost.

Reverse Dijkstra produces shared destination guidance, including downstream
costs. Its chosen successor always points toward an earlier settled state,
so zero-cost cycles terminate. It uses reachable local states rather than a
common dense coordinate lattice. Hierarchical caches are deferred until this
exact baseline is measured; functional shared guidance is required now.

## Ownership and bounded work

Snapshot preparation is explicit authoring work, separate from query work.
Published domains and all transitively referenced policy/world data are frozen;
shared ownership retains memory but does not synchronize mutation. Query and
queue access require external synchronization. No hidden worker threads.

Each query owns its scratch. Admission limits discovered labels, frontier
entries and output steps. Initialization, one edge probe, one frontier pop,
one reconstruction append, one reversal swap and publication each consume
work. Ordered lookup and heap operations have logarithmic bounds in admitted
capacity, not in unbounded input degree. Allocator calls and caller callbacks
are outside the deterministic work allowance; this is not a time guarantee.
Reserved arrays avoid relocation loops during query steps. Output publication
moves owned storage; it never copies an entire completed product in one step.

Reuse AsyncWorkBudget/AsyncWorkStep and ResumableWorkQueue via a callback adapter.
The queue's non-owning continuation lifetime and retained-slot scans remain
explicit; the search kernel's bound is not a bound for the entire queue.
Cancellation and supersession use existing lifecycle states and publish no
partial product. Input replacement marks pending work stale. Result outcome
separately distinguishes no route, unknown topology, invalid input, overflow
and capacity refusal. General allocation and callback exceptions retain the
existing integration policy; there is no universal bad_alloc recovery claim.

Unknown detail weakens proof: a found route can be feasible without whole-world
optimality, and exhaustion cannot prove no route through unavailable detail.
Snapshot completeness is conservative. Route products retain locations and
selected transitions; guidance owns labels and successors. Memory accessors
report logical retained storage, not whole-process peak memory.

## Verification

Test first with an independently materialized small weighted graph oracle.
Cover asymmetric and parallel edges, leave/re-enter routes, zero cycles,
unequal downstream entrance costs, graph/grid/grid in both directions,
different schemas/scales, blocked physical joins, and unknown sparse detail.
Use one-item budgets through initialization, high-degree enumeration and long
reconstruction; compare synchronous and resumed output. Check refusal before
partial publication, retained storage after scratch destruction, snapshot and
profile replacement, provider/domain lifetime reuse, and queue cancellation.

Run existing relevant grid suites unchanged, header/surface/docs checks,
installed consumption, exception-free/no-RTTI checks and sanitizers. Broader
platform and real downstream motion acceptance remain separately recorded
qualification, never inferred from these synthetic tests.
