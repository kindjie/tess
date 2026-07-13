# Benchmark Trends

Stale snapshot: data from CI run 29211536546 at commit cd22285, collected 2026-07-12 16:09 PDT.

Thresholds were recalibrated 2026-07-11 (S11.3) from ten reworked-harness
main-run baseline artifacts (runs 29056942917 through 29167134881, 10
repetitions each, so 60-100 samples per benchmark; per-benchmark CVs
mostly 6-15%): every gate is now twice the maximum observed across all
samples, tightened from the previous single-artifact 3x policy per the
10-artifact rule. The 2x headroom (rather than the summary tool's 1.5x
default) absorbs the heterogeneous-CPU spread of the shared-runner pool,
and nanosecond-scale gates carry an additional absolute floor of 25 ns:
below that, 2x-of-observed assumes the artifact window sampled the
slowest runner SKU, and a merely-slower machine fails a correct
benchmark (observed empirically: an independent reviewer's VM measured
`key/tile_key_2d_u64` at 10.3 ns against the pre-floor 8 ns ceiling).
Nanosecond gates exist to catch gross regressions -- an accidental
allocation, lock, or complexity change lands 5-100x, well above the
floor.
The scheduler, ecs, render-delta, and fields families keep their
bootstrap ceilings: their baselines were not collected by the
`tess_bench_ci_baselines` target until S11.3 wired them in, so they are
recalibrated only once enough artifacts carrying them accumulate
(10-artifact rule; wired 2026-07-11).
The audit-2026-07-11 remediation (merged 2026-07-12) de-elided five
storage/block benches whose loops previously compiled away, so their
history has an intended discontinuity at that date -- pre-audit samples
of those benches measure nothing. Those five, the new `parallel/` and
`residency/` families, `storage/world_dirty_chunks_iteration_16k`
(v0.2 SoA-split evidence), and
`path/agent_tick_100_weighted_goal_churn_512x512` (per-agent pathing
dirt) all sit on x6-local bootstrap ceilings awaiting the same
10-artifact recalibration.

The SVG snapshot in `docs/assets/benchmark-trends.svg` is a labeled
summary, not the source of truth. Use CI benchmark baseline artifacts
and threshold JSON files for calibration decisions.

Regenerate the detailed report with:

```sh
tools/benchmark_trends.py path/to/benchmark-baselines-* \
  --out build/bench/benchmark-trends.html \
  --snapshot-svg docs/assets/benchmark-trends.svg \
  --summary-md build/bench/benchmark-trends.md
```

Use the generated Markdown as a scratch summary. Update this maintained page by
copying over the stale label and latest snapshot table while preserving the
regeneration policy.

For individual optimization experiments, rejected ideas, and deferred
performance work, use the [optimization log](planning/optimization-log.md)
instead of architecture docs.

## Regeneration Policy

Regenerate and commit `docs/assets/benchmark-trends.svg` and this page when the
snapshot will materially help readers understand current performance:

- before enabling or changing benchmark timing thresholds
- after benchmark workloads, selected trend benchmarks, or threshold JSON names
  change
- after a performance-sensitive optimization or regression fix
- at milestone or release checkpoints
- after collecting at least 5 CI baseline artifacts, or 10 artifacts before
  tightening existing limits

Do not refresh the snapshot for every CI run. A stale snapshot is acceptable
when its label shows the source CI run, commit, and Pacific-time timestamp.

## Automation Boundary

CI already automates benchmark collection by uploading baseline JSON artifacts
for each run. The trend tool automates summarizing those artifacts and
regenerating the SVG, HTML report, and Markdown summary.

The final commit remains manual by design:

- CI should not push generated README images back to branches.
- Maintainers should review trend shape, variance, and benchmark relevance
  before changing tracked performance docs.
- Threshold JSON files remain the authoritative gate policy; plots are
  calibration evidence.

## Latest Snapshot

![Benchmark trend snapshot](assets/benchmark-trends.svg)

| Benchmark | Latest median CPU ns |
| --- | ---: |
| `block/context_iteration_2d` | 277.272 |
| `block/chunk_tile_iteration_2d` | 1484.974 |
| `block/chunk_boundary_scan_2d` | 11004.955 |
| `storage/world_chunks_iteration` | 165.552 |
| `storage/world_dirty_chunks_iteration` | 199.969 |
