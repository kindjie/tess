# Benchmark Trends

Stale snapshot: data from CI run 26980713740 at commit 90ce419, collected 2026-06-04 14:33 PDT.
Scratch-specific block benchmarks were added after this snapshot and will not
appear until the next intentional CI-baseline refresh. Queued execution and
MVP path benchmarks were also added after this snapshot. Since then the
benchmark harness was reworked (2026-07-06): several benchmarks were fixed to
measure the named operation, batch counters were renamed, the field-product
cache benchmarks gained `/iterations:N/manual_time` names gated on real time,
and `bench/thresholds/*.json` ceilings changed accordingly. The snapshot below
predates all of that and must not be used for calibration.

TODO(regenerate-snapshot): after CI has collected at least 5 baseline
artifacts from the reworked harness, regenerate the SVG and this table from
those artifacts per the regeneration policy below (the threshold change is
the trigger; the recalibrated CI baselines are the missing input).

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
| `block/context_iteration_2d` | 436.332 |
| `block/chunk_tile_iteration_2d` | 1710.011 |
| `block/chunk_boundary_scan_2d` | 12332.274 |
| `storage/world_chunks_iteration` | 93.393 |
| `storage/world_dirty_chunks_iteration` | 285.844 |
