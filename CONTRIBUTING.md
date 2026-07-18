# Contributing to tess

Thanks for working on `tess`. This document collects the developer
workflow: presets, hooks, quality gates, and benchmark policy. For
formatting and layout conventions, read [`docs/style.md`](docs/style.md).

## Workflow expectations

- New or modified functionality lands with tests, and the full suite
  passes before merge - no exceptions.
- Meaningful design changes get an entry in
  [`docs/decisions/CHANGELOG.md`](docs/decisions/CHANGELOG.md); the
  top-level [`CHANGELOG.md`](CHANGELOG.md) records release-facing
  changes under `Unreleased`.
- Markdown stays near 80 columns; docs separate maintained material
  from the historical TDD archive (see [`docs/README.md`](docs/README.md)).

## Development setup

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

The `dev` preset also builds the examples (each a self-checking binary,
smoke-run in CI). Other presets: `dev-werror`, `dev-asan`, `dev-tsan`,
`release`, `bench`, `bench-profile`, `windows-msvc`, `dev-clang-tidy`,
`dev-cppcheck`, and `dev-clang-tidy-advisory`.

Install the local git hooks before your first commit:

```sh
python3 tools/git_hooks.py install
```

The hooks enforce repository hygiene (see
[`docs/git-hooks.md`](docs/git-hooks.md)): no email addresses, absolute
local paths, or other personal identifiers in any committed file, plus
per-file token limits. CI runs the same checks as a backstop.

## Quality gates

Project warnings are errors in the `dev-werror`, `dev-asan`, `dev-tsan`,
`release`, `bench`, `bench-profile`, and `windows-msvc` presets. CI also
uses `dev-werror` for the required GCC portability build.

CI runs primarily on `ubuntu-24.04` with Clang and covers:

- Dev build and unit tests: `cmake --build --preset dev`,
  `ctest --preset dev`
- Installed header file-set drift check: `tess_installed_headers_file_set`
- Installed package smoke test: `tools/install_smoke.sh`
- Hook backstop checks: `tools/git_hooks.py ci` repository hygiene plus
  pytest for the repo tools (`tests/test_git_hooks.py`,
  `tests/test_benchmark_tools.py`, `tests/test_check_public_surface.py`)
  and the bidirectional public-surface manifest gate
  (`tools/check_public_surface.py` against
  `docs/architecture/surface.json`; required since 2026-07-07)
- Installed-header namespace-scope Doxygen gate: `tools/check_public_docs.py`
- Warnings-as-errors build and tests: preset `dev-werror`
- ASan/UBSan build and tests (UBSan findings are fatal): preset `dev-asan`
- TSan build and tests (`TSAN_OPTIONS=halt_on_error=1`): preset `dev-tsan`
- Release build and tests: preset `release`
- macOS build, tests, and install smoke on `macos-15`: presets `dev` and
  `dev-asan` (no benchmark gates there; thresholds are Linux-calibrated)
- Windows MSVC build, tests, and install smoke on `windows-2025`:
  preset `windows-msvc` (required gate since 2026-07-07)
- Strict clang-tidy gate: `cmake --build --preset dev-clang-tidy`
- cppcheck gate: `cmake --build --preset dev-cppcheck`
- Advisory (non-gating) clang-tidy profile: preset `dev-clang-tidy-advisory`
- Required GCC compile-only portability check: preset `dev-werror` built with
  GCC
- Benchmark build and smoke tests: preset `bench`
- Benchmark threshold gates, one per suite (CPU time except parallel wall
  time):
  `cmake --build --preset bench --target tess_bench_<suite>_thresholds`
  for `key`, `storage`, `block`, `queued`, `path`, `topology`, `scheduler`,
  `residency`, `parallel`, `ecs`, `render_delta`, `fields`, and `diagnostics`
- Non-gating CI benchmark baseline collection:
  `cmake --build --preset bench --target tess_bench_ci_baselines`

## Benchmarks

Benchmarks are opt-in and use Google Benchmark:

```sh
cmake --preset bench
cmake --build --preset bench
./build/bench/bench/tess_bench
```

Benchmark thresholds enforce calibrated per-benchmark time ceilings
(`bench/thresholds/*.json`) and fail CI when exceeded or when an expected
benchmark is missing. Most suites gate CPU time because shared-runner
wall time is noisy; wall time is gated where CPU time understates the
work (the parallel pool suite and manually timed benchmarks).
Recalibrate from CI baseline artifacts after intentional performance
changes; summarize downloaded artifacts with:

```sh
tools/benchmark_baseline_summary.py path/to/*.json
```

See [`docs/performance.md`](docs/performance.md) for the trend snapshot
workflow and when to regenerate the README image.

Prefer the narrowest public header that owns the API in compile-sensitive
code. To compare syntax-only header costs on the local compiler, run:

```sh
python3 tools/header_compile_cost.py \
  tess/tess.h tess/core/shape.h tess/path/path.h
```

## Testing on a Steam Deck

Build on macOS (for x86_64, in Valve's Steam Runtime container) and run the
tests or benchmarks on real Steam Deck hardware via `tools/steamdeck/deck` -
see [`tools/steamdeck/README.md`](tools/steamdeck/README.md). Quickstart:

```sh
tools/steamdeck/deck setup && tools/steamdeck/deck deck-setup   # once
tools/steamdeck/deck bench --pin                                # run on the Deck
```
