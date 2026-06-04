# Benchmark Trends

Stale snapshot: data from CI run 26975937015 at commit fb7e3bf, collected 2026-06-04 12:59 PDT.

The SVG snapshot in `docs/assets/benchmark-trends.svg` is a labeled
summary, not the source of truth. Use CI benchmark baseline artifacts
and threshold JSON files for calibration decisions.

Regenerate the detailed report with:

```sh
tools/benchmark_trends.py path/to/benchmark-baselines-* \
  --out build/bench/benchmark-trends.html \
  --snapshot-svg docs/assets/benchmark-trends.svg \
  --summary-md docs/performance.md
```

## Latest Snapshot

![Benchmark trend snapshot](assets/benchmark-trends.svg)

| Benchmark | Latest median CPU ns |
| --- | ---: |
| `block/context_iteration_2d` | 402.730 |
| `block/chunk_tile_iteration_2d` | 1709.727 |
| `block/chunk_boundary_scan_2d` | 12319.091 |
| `storage/world_chunks_iteration` | 93.382 |
| `storage/world_dirty_chunks_iteration` | 279.552 |
