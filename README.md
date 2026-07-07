# tess

[![CI](https://github.com/kindjie/tess/actions/workflows/ci.yml/badge.svg)](https://github.com/kindjie/tess/actions/workflows/ci.yml)

`tess` is a performance-first C++ tile and path simulation substrate.

The project has an implemented synchronous MVP foundation. Documentation lives
in `docs/`. The original TDDs are archived under `docs/tdd/`; maintained
architecture docs track the current implementation as code lands.

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

CI runs primarily on `ubuntu-24.04` with Clang and covers:

- Dev build and unit tests: `cmake --build --preset dev`,
  `ctest --preset dev`
- Public header file-set drift check: `tess_public_headers_file_set`
- Installed package smoke test: `tools/install_smoke.sh`
- Warnings-as-errors build and tests: preset `dev-werror`
- ASan/UBSan build and tests (UBSan findings are fatal): preset `dev-asan`
- TSan build and tests (`TSAN_OPTIONS=halt_on_error=1`): preset `dev-tsan`
- Release build and tests: preset `release`
- macOS build, tests, and install smoke on `macos-15`: presets `dev` and
  `dev-asan` (no benchmark gates there; thresholds are Linux-calibrated)
- Advisory Windows MSVC build, tests, and install smoke on `windows-2025`:
  preset `windows-msvc` (non-gating during shake-out)
- Strict clang-tidy gate: `cmake --build --preset dev-clang-tidy`
- cppcheck gate: `cmake --build --preset dev-cppcheck`
- Advisory (non-gating) clang-tidy profile: preset `dev-clang-tidy-advisory`
- Benchmark build and smoke tests: presets `bench`
- Benchmark CPU-time threshold gates, one per suite:
  `cmake --build --preset bench --target tess_bench_<suite>_thresholds`
  for `key`, `storage`, `block`, `queued`, and `path`
- Non-gating CI benchmark baseline collection:
  `cmake --build --preset bench --target tess_bench_ci_baselines`

Benchmark thresholds enforce calibrated per-benchmark CPU-time ceilings
(`bench/thresholds/*.json`) and fail CI when exceeded or when an expected
benchmark is missing. Wall-clock ceilings stay unset because shared-runner
wall time is too noisy to gate. Recalibrate from CI baseline artifacts after
intentional performance changes; summarize downloaded artifacts with:

```sh
tools/benchmark_baseline_summary.py path/to/*.json
```

New contributors: install the local git hooks with
`python3 tools/git_hooks.py install` (see
[`docs/git-hooks.md`](docs/git-hooks.md)) and read
[`docs/style.md`](docs/style.md) for formatting and layout conventions.

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
