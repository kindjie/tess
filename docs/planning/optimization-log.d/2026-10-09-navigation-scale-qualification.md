## 2026-10-09 - Composable navigation scale qualification workloads

**Decision:** keep the exact Dijkstra baseline and the advisory status of
`lab/navigation/`; add the `scale_*` cells as the qualification fixture that
future hierarchical-cache or admission work must beat on controlled hardware.
No timing threshold is set, no paired baseline is claimed, and the directory
stays unrepresented in paired sentinels.

The earlier composed workload compared full routes against cost-only reads.
The new cells extract complete ordered transitions in both cases. Equivalence
here means: for the same requests, both cases extract complete, legal,
oracle-optimal ordered transitions with equal per-request cost; they often
follow different equal-cost paths.
One heterogeneous composition joins a 32x32 unit yard (16x16 chunks, scale
4, pillar obstacles), a 32x32 weighted rubble grid with diagonal steps (8x8
chunks, scale 1, hashed weights with about 10% blocked), a fully resident
16x16 sparse cellar (8x8 chunks, scale 3, one wall with two gaps) and a
64-node directed road graph (ring, chords, one cheaper parallel edge, one
blocked edge) through 41 directed attachments with asymmetric costs and one
blocked parallel seam: 2368 positions, 2199 legal states. Limits are
{2560, 16384, 2560} except the refusal cells. Populations are 32/256/1024
requests over 4/16/64 distinct goals, request i targeting goal i mod goals,
drawn by a fixed generator over all legal states.

An independent oracle materializes the authored edges from coordinate
arithmetic and the documented cost rules (destination entry cost times step
multiplier times scale; diagonals need both corners clear; grid edge ids are
the geometric candidate ordinals) and runs an array/heap Dijkstra per goal
outside every timed loop. After timing, every route and every guidance walk
is re-extracted and checked step by step for chaining, original endpoints,
id, key, cost, legal availability, seam flag, termination at the goal and
total cost, and every shared field is checked label by label against the
oracle distances. The timed consumption folds each transition and request
total into an order-sensitive hash that must equal the checked pass's fold,
so a timed loop that skips or shortens extraction aborts. Five compiled
mutants were rejected by that pass while the unmodified source passed:
shortened timed walk, skipped timed route extraction, remaining cost consumed
as step cost, grid edge ids offset by 10000, and exported downstream costs
off by one.

Deterministic counters (independent of timing): repeated routes used
236,015 / 2,172,853 / 7,952,619 work items for 32/256/1024 requests and
extracted 1,180 / 9,561 / 39,785 transitions; shared guidance used 75,847 /
303,594 / 1,214,736 for 4/16/64 fields and its walks extracted 1,178 / 9,561
/ 39,782 transitions. Per-request costs are equal and oracle-optimal in both
cases, but a request-by-request comparison shows 19 of 32, 135 of 256 and
619 of 1024 requests following different equal-cost paths (449 / 2,116 /
10,398 positional transition mismatches, zero cost mismatches). Logical
retained payload was 844,416 / 3,377,664 / 13,510,656 bytes for the fields
versus a peak of 5,312 to 6,336 bytes for one retained route at a time.
Admission refusal under {256, 256, 256} spent 42,972 work items on 32 routes
plus one field and published no partial product; on an open 256x256 unit
grid, the corner-to-corner route needed 392,960 work items with room for
65,536 labels, while a 4,096-label cap refused after 23,860. Both refusal
cells assert that refusal work stays under a bound derived from the
configured limits and a conservative degree cap (186,021 and 40,965
items, 4.3x and 1.7x the observed refusal work), so a refusal that
approached a full search would fail the benchmark.
Cold cells also time release of the previous iteration's retained snapshot
and fields; the phase cells partition the cold cells without double counting,
with production route reconstruction inside the query phase.

Allocator counts from the diagnostics hooks (an allocator measurement, not
payload and not process RSS): a cold 32-request route cell made 37,825
allocations totalling 22.6 MB, about 0.7 MB per route query, dominated by the
reserved frontier and route ceilings; the cold 32-request shared cell made
18,156 allocations totalling 3.9 MB. Warm walks and extraction allocate
nothing. The hooks subtract nothing for an unsized delete, so peak and
retained live bytes are upper bounds; on this host's AppleClang 21 they equal
the cumulative total because the compiler does not enable sized deallocation
by default, whereas GCC and upstream Clang 19 and later do. With
`-fsized-deallocation` added to the diagnostics build for this measurement
(for example through `CMAKE_CXX_FLAGS` when configuring the `bench` preset,
then running `tess_bench_diagnostics` on `^lab/navigation/scale_`), peak live
bytes were at most 0.87 MB for route cells at every population (one query's
reservations) against at most 1.8 / 5.2 / 18.7 MB for shared cells, whose
retained fields were at most 1.16 / 4.5 / 18.1 MB against 0.84 / 3.4 / 13.5
MB of logical payload; the prepared snapshot and worlds retain about 32 KB.
The shared_replace cell releases its stale fields inside the scope by design,
so the hook's clamp drops the replacement snapshot's 32 KB from its retained
figure (4,505,344 bytes against 4,537,728 for the cold shared cell).

Timings were collected in a single unrepeated run on a 16-core host at load
averages between 87 and 159; per-iteration CPU means ranged from about 27 ms
(32 cold routes) to 0.87 s (1024 cold routes, one iteration) against 9 ms to
141 ms for the shared cells, and warm walks took 45 us to 2.8 ms. These
numbers are inconclusive for performance.

**Deferred:** controlled paired timing of the unchanged standalone grid
sentinels and of these cells on an idle host. The heterogeneous composition
was not scaled in world size: every composition cell uses the same 2,368
positions and only the request and goal counts grow, and the comparison of
repeated routes with shared guidance exists only at that one geometry. World
size scales only in the single-grid, single-request admission cells. Also
deferred: larger worlds than 256x256, chunk-eviction residency states,
hierarchical or cached guidance, and downstream journey execution.
