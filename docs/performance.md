# Performance

tess is performance-first: every benchmark suite is gated in CI with
calibrated per-benchmark ceilings, so the numbers below are enforced, not
aspirational.

Representative medians from the benchmark suite on an Apple M3 Max
(single-threaded):

- A* across an open 512x512 grid, corner to corner (a 1,022-step path,
  ~1,023 nodes expanded): ~2.1 us; the weighted variant: ~2.4 us.
- One clean tick of 100 path agents with retained routes: ~330 ns.
- One `weighted_path_batch` plan of 100 near-goal requests on a 512x512
  grid: ~50 us.

Memory, not time, but enforced the same way: `examples/sparse_stream.cc`
self-checks a 1,024-chunk world held to a 16-page residency budget — 64x
less resident field storage than the dense equivalent (1 MiB vs 16 KiB
of page data; residency metadata is budgeted separately).

## When the worker pool is worth it

The parallel phase executor is not free: dispatching a phase costs about
the same as a chunk of trivial work. Whether the pool beats the serial
executor is decided almost entirely by **how much work each chunk does**,
not by how many chunks there are.

![Serial versus pool speedup against work per chunk](assets/thread-scaling-crossover.svg)

**At four workers**, on the machine below:

- **Above roughly 45 ns of work per chunk the pool wins**, and keeps
  winning as the work grows — 1.2x at 45 ns, 1.9x at 97 ns, and 4.0x for
  a compute-bound chunk.
- **At roughly 12 ns per chunk it loses badly**: a one-tile-per-chunk
  phase runs about 3x *slower* through the pool than serially.
- **The crossover is bracketed, not pinned.** The nearest measured points
  either side are 11.5 ns, which loses, and 44.8 ns, which wins; nothing
  between them was measured, and nothing below 11.5 ns was measured at
  all.

A practical starting point: if a chunk does more than about 50 ns of
work, use the pool. You can read your own figure off a serial run —
total phase time divided by chunk count — then measure.

Design against the upper figure. The lower one is a single measured
point rather than a boundary, and where the crossover sits at other
worker counts was not measured in this campaign.

**Scope.** These are figures from one machine under controlled
conditions, not portable constants.

| | |
| --- | --- |
| CPU | Intel Xeon Platinum 8481C, 2 sockets x 48 cores x 2 threads |
| Topology | 4 NUMA nodes, 105 MiB L3 per socket |
| Instance | `c3-standard-192-metal` |
| Clock | `performance` governor, turbo enabled |
| Memory policy | `numactl --interleave=all` |
| Pinning | one worker per physical core, plus a CPU for the dispatcher |
| World | 4096 chunks of 64x64 tiles |
| Method | 20 repetitions per point; speedup against `SerialPhaseExecutor` measured in the same process under the same pinning |
| Measured | 2026-08-04, one campaign |

The governor and the memory policy matter as much as the CPU: without
them the same code on the same machine measures differently, which is
why they are stated rather than assumed. Two-worker results are withheld: they are too noisy in campaign
conditions to state. Beyond about 24 workers the measurements are too
noisy to publish at all — see
the [optimization log][optimization-log] for the full campaign record,
including what these numbers do not show.

## Trend snapshot

Data from CI run 29211536546, collected 2026-07-12 16:09 PDT; the snapshot
may be stale by a few commits.

![Benchmark trend snapshot](assets/benchmark-trends.svg)

| Benchmark | Latest median CPU ns |
| --- | ---: |
| `block/context_iteration_2d` | 277.272 |
| `block/chunk_tile_iteration_2d` | 1484.974 |
| `block/chunk_boundary_scan_2d` | 11004.955 |
| `storage/world_chunks_iteration` | 165.552 |
| `storage/world_dirty_chunks_iteration` | 199.969 |

CI collects baseline artifacts on every main run; these medians come from
those artifacts. Individual optimization experiments, rejected ideas, and
deferred performance work are recorded in the
[optimization log][optimization-log]; threshold calibration methodology is
recorded in the [calibration history][calibration]. The regeneration
workflow for this page lives in [`CONTRIBUTING.md`][contributing].

[optimization-log]: https://github.com/kindjie/tess/blob/main/docs/planning/optimization-log.md
[calibration]: https://github.com/kindjie/tess/blob/main/docs/planning/benchmark-calibration.md
[contributing]: https://github.com/kindjie/tess/blob/main/CONTRIBUTING.md
