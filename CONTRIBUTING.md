# Contributing to tess

Thanks for working on `tess`. This document collects the developer
workflow: presets, hooks, quality gates, and benchmark policy. For
formatting and layout conventions, read [`docs/style.md`](docs/style.md).
Participation in the project is covered by the
[code of conduct](CODE_OF_CONDUCT.md).

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
`dev-cppcheck`, and `dev-clang-tidy-advisory`. (The `consumer` preset is
the user-facing install path documented in the README, not a dev
preset.)

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

## Documentation

The documentation site is MkDocs Material plus a Doxygen API reference.
Preview the site locally:

```sh
python3.12 -m venv .venv-docs
.venv-docs/bin/python -m pip install \
  --require-hashes --requirement requirements-docs.txt
.venv-docs/bin/mkdocs serve
```

Generate the API reference locally with
`cmake --preset consumer -DTESS_BUILD_DOCS=ON` followed by
`cmake --build build/consumer --target tess_docs` (requires Doxygen);
output lands in `build/consumer/docs/html`. Its theme is the vendored
[doxygen-awesome-css](docs/doxygen-awesome/README.md).

Documentation is CI-enforced, so adopter pages do not need to say so:
`tess-snippet` blocks are byte-synchronized with compiled sources
(`tools/check_doc_snippets.py`), `tess-output` blocks are compared against
real program stdout (`tools/check_doc_outputs.py`), version statements are
cross-checked against the CMake source version
(`tools/check_doc_versions.py`), and every generated-site link and anchor
is validated (`tools/check_docs_links.py`). Deployment and DNS live in
[`docs/hosting.md`](docs/hosting.md); the documentation tree map lives in
[`docs/README.md`](docs/README.md).

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

### Trend snapshot regeneration

Regenerate the detailed report with:

```sh
tools/benchmark_trends.py path/to/benchmark-baselines-* \
  --out build/bench/benchmark-trends.html \
  --snapshot-svg docs/assets/benchmark-trends.svg \
  --summary-md build/bench/benchmark-trends.md
```

Regenerate and commit `docs/assets/benchmark-trends.svg` and the snapshot
table in [`docs/performance.md`](docs/performance.md) when the snapshot
will materially help readers understand current performance:

- before enabling or changing benchmark timing thresholds
- after benchmark workloads, selected trend benchmarks, or threshold JSON
  names change
- after a performance-sensitive optimization or regression fix
- at milestone or release checkpoints
- after collecting at least 5 CI baseline artifacts, or 10 artifacts
  before tightening existing limits

Do not refresh the snapshot for every CI run. A stale snapshot is
acceptable when its label shows the source CI run, commit, and
Pacific-time timestamp.

The final commit remains manual by design: CI should not push generated
images back to branches, maintainers should review trend shape, variance,
and benchmark relevance before changing tracked performance docs, and the
threshold JSON files remain the authoritative gate policy — plots are
calibration evidence. Calibration methodology and history live in
[`docs/planning/benchmark-calibration.md`](docs/planning/benchmark-calibration.md);
individual optimization experiments live in
[`docs/planning/optimization-log.md`](docs/planning/optimization-log.md).

Prefer the narrowest public header that owns the API in compile-sensitive
code. To compare syntax-only header costs on the local compiler, run:

```sh
python3 tools/header_compile_cost.py \
  tess/tess.h tess/core/shape.h tess/path/path.h
```

## Testing on a Steam Deck

The Steam Deck is the project's fixed-hardware x86_64 (Zen 2) Linux
target for hardware-accurate benchmarks and on-device validation —
numbers there are reproducible in a way shared CI runners cannot be.

Build on macOS (for x86_64, in Valve's Steam Runtime container) and run the
tests or benchmarks on real Steam Deck hardware via `tools/steamdeck/deck` -
see [`tools/steamdeck/README.md`](tools/steamdeck/README.md). Quickstart:

```sh
tools/steamdeck/deck setup && tools/steamdeck/deck deck-setup   # once
tools/steamdeck/deck bench --pin                                # run on the Deck
```
