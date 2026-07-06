# tess

[![CI](https://github.com/kindjie/tess/actions/workflows/ci.yml/badge.svg)](https://github.com/kindjie/tess/actions/workflows/ci.yml)

`tess` is a performance-first C++ tile and path simulation substrate.

The project is currently in design/scaffolding. Documentation lives in `docs/`.
The original TDDs are archived under `docs/tdd/`; maintained architecture docs
will track implementation as code lands.

## Build

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

Benchmarks are opt-in and use Google Benchmark:

```sh
cmake --preset bench
cmake --build --preset bench
./build/bench/bench/tess_bench
```

The public CMake target is `tess::tess`.

The `dev` preset also builds `examples/tess_mvp_path`, a small end-to-end
queued-edit plus A* pathfinding prototype.

## Testing on a Steam Deck

Build on macOS (for x86_64, in Valve's Steam Runtime container) and run the
tests or benchmarks on real Steam Deck hardware via `tools/steamdeck/deck` — see
[`tools/steamdeck/README.md`](tools/steamdeck/README.md). Quickstart:

```sh
tools/steamdeck/deck setup && tools/steamdeck/deck deck-setup   # once
tools/steamdeck/deck bench --pin                                # run on the Deck
```

## Quality Gates

CI runs on `ubuntu-24.04` with Clang and covers:

- Dev build: `cmake --build --preset dev`
- Unit tests: `ctest --preset dev`
- Public header file-set drift check: `tess_public_headers_file_set`
- Installed package smoke test: `tools/install_smoke.sh`
- Strict clang-tidy gate: `cmake --build --preset dev-clang-tidy`
- Benchmark build: `cmake --build --preset bench`
- Benchmark smoke tests: `ctest --preset bench`
- Key conversion benchmark threshold scaffold:
  `cmake --build --preset bench --target tess_bench_key_thresholds`
- Storage benchmark threshold scaffold:
  `cmake --build --preset bench --target tess_bench_storage_thresholds`
- Block benchmark threshold scaffold:
  `cmake --build --preset bench --target tess_bench_block_thresholds`
- Queued benchmark threshold scaffold:
  `cmake --build --preset bench --target tess_bench_queued_thresholds`
- Path benchmark threshold scaffold:
  `cmake --build --preset bench --target tess_bench_path_thresholds`
- Non-gating CI benchmark baseline collection:
  `cmake --build --preset bench --target tess_bench_ci_baselines`

Benchmark thresholds are currently scaffolded with disabled wall-clock limits
until stable same-runner baselines are available. CI uploads repeated benchmark
JSON samples as a workflow artifact for threshold calibration.
Summarize downloaded baseline artifacts with:

```sh
tools/benchmark_baseline_summary.py path/to/*.json
```

## Benchmark Trends

![Benchmark trend snapshot](docs/assets/benchmark-trends.svg)

This snapshot is intentionally labeled with its source CI run, commit, and
Pacific-time collection timestamp. It may be stale by a few commits. See
[`docs/performance.md`](docs/performance.md) for when and how to regenerate it,
and use CI benchmark artifacts as the source of truth for threshold
calibration.

## Name

`tess` is named after tesserae and tessellation: small spatial pieces composed
into large, structured worlds for fast simulation, topology, and pathfinding.
