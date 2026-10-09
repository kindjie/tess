## 2026-10-08 - Exact composable navigation baseline

**Decision:** retain exact resumable Dijkstra as the experimental correctness
baseline. Defer hierarchical caches until representative composed-world
measurements demonstrate their need. No timing acceptance threshold is set by
this screening run.

The new advisory workloads use an explicit 256-node graph with 510 directed
edges. Route end-to-end includes graph/snapshot preparation and a retained
255-transition route. Shared-goal construction includes preparation, retained
reverse guidance and 256 reads. Checked results were cost 255 and guidance
cost sum 32640, respectively. Query counters were 1408 / 1284 work items and
16320 / 24576 logical payload bytes; these exclude shared snapshot storage and
allocator overhead.

Three short repetitions on an Apple Silicon development machine produced CPU
medians of approximately 218 / 240 microseconds. Concurrent load exceeded the
available CPU count substantially; elapsed medians were approximately 1.8 /
3.2 milliseconds. These measurements are **inconclusive for performance** and
are not release or downstream scale qualification.

Unchanged standalone diagonal, hex and stair-provider workloads were compiled
against the fetched baseline and the experimental checkout with the same
compiler/options. Their cost, expanded-node and path-node counters matched
exactly across three repetitions. Contended CPU medians were approximately
27.7 / 26.0, 30.8 / 30.7 and 2989 / 2981 microseconds (baseline / candidate).
No regression or improvement claim follows from those short timings.

A second workload joins two native 16x16 grids with different field schemas
and costs through a two-node directed graph and zero-cost connections. Both
cases include one snapshot preparation and 32 requests to one destination.
Sixteen unique origins are each requested twice. Independent Manhattan
arithmetic gives an aggregate cost of 2800 in both cases. Routes materialize
full paths while the shared case reads only starting costs from its field;
this compares remaining-cost answers, not equal journey extraction/execution.
Repeated routes used 97280 deterministic work items; shared guidance
used 3474. Those counts exclude setup and guidance lookup work.
Peak logical retained payload was 4032 bytes for one route at a time
versus 49344 bytes for the shared field. These counters demonstrate reuse on
this small uniform fixture, not whole-process memory or city-scale behavior.
Warnings-as-errors compilation and all benchmark correctness checks passed.

Three short repetitions gave CPU medians approximately 8.86 ms / 0.247 ms
for repeated routes / shared guidance. Load averages remained above 76; these
are still advisory screening timings, not a calibrated speedup claim.

**Deferred:** controlled paired timing; representative heterogeneous mixtures;
large-population shared-destination amortization; retained/transient peak
memory; large-world admission; platform and actual downstream journey
qualification.
The lab workloads remain advisory and the new experimental directory is
explicitly unrepresented in paired sentinels until suitable calibration exists.
Existing shared infrastructure retains its prior sentinel selection.
