# Performance

tess is performance-first: every benchmark suite is gated in CI with
calibrated per-benchmark ceilings, so the numbers below are enforced, not
aspirational.

Representative medians from the benchmark suite on an Apple M3 Max
(single-threaded):

- A* across an open 512x512 grid, corner to corner (a 1,022-step path,
  ~1,023 nodes expanded): ~2.1 us; the weighted variant: ~2.4 us.
- One clean tick of 100 path agents with retained routes: ~330 ns.

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
