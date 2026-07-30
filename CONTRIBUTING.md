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

CI is tiered (see
[`docs/planning/test-and-benchmark-redesign.md`](docs/planning/test-and-benchmark-redesign.md)
section 5): pull requests block on the dev, GCC, hook-backstop, ASan,
cppcheck, Windows, and benchmark compile+smoke jobs, plus a diff-scoped
clang-tidy job (`tools/clang_tidy_changed.py`) and a TSan job that runs
when concurrency-sensitive paths change (`tools/ci_changes.py`). Pushes
to main, the weekly scheduled run, and manual dispatches additionally
run werror, release, TSan, macOS, the full-tree clang-tidy sweep, and
the benchmark threshold gates; a failed non-PR run files or extends a
rolling `ci-failure` issue.

Pull requests touching perf-sensitive paths also run a **shadow-mode
paired sentinel benchmark job** (`tools/paired_bench.py` over
`bench/sentinels.json`): base and head binaries interleave a small
composite sentinel set and the verdict lands in the job's step summary
and artifact. Shadow means informational — it never blocks a merge; the
calibrated threshold gates remain authoritative. A suspected regression
can be confirmed between two explicit commits with the "Paired sentinel
confirmation" dispatch workflow, which does fail on a confirmed
regression.

**Counter goldens** (also shadow mode): the ctest fixture pair
`TessCounterGoldenProbe`/`TessCounterGoldenCheck` compares deterministic
work counters from fixed serial workloads against
`tests/goldens/counters.json`. Behavioral drift is advisory — it prints,
lands in the dev job's step summary, and never fails the suite — but
probe failures, malformed documents, and tooling errors still fail like
any test. When a change intentionally moves the counters, regenerate the
golden in the same pull request:

```sh
build/dev/tests/tess_counter_golden_probe /tmp/observed.json
tools/check_counter_goldens.py --observed /tmp/observed.json \
  --golden tests/goldens/counters.json --update
```

Setting `TESS_COUNTER_GOLDENS_STRICT=1` makes drift fail (the redesign's
phase 4 promotion path).

**Workload matrix**: `bench/workload-matrix.json` declares which
workload cells (world extent x chunk extent x layout x storage x
executor x payload) the benchmark suite measures, via per-family name
grammars plus curated defaults. Adding, renaming, or removing a
benchmark must keep it coherent — `tools/check_workload_matrix.py
--catalog bench/workload-matrix.json --thresholds-dir bench/thresholds
--bench-sources bench` runs the static check locally (CI also checks
the compiled registration union in the bench job). A new benchmark
that fits an existing family grammar needs no catalog edit; new
vocabulary or an unmatched name fails until classified.

**Advisory coverage** (weekly tier, never gating): the scheduled run
publishes an llvm-cov summary of the test suite and a benchmark
gap-finder report — which public headers no benchmark executes
(`tools/coverage_gaps.py`; acknowledged gaps live in
`tools/coverage_known_gaps.json` with reasons). Reproduce locally with
Clang:

```sh
cmake --preset bench-coverage && cmake --build --preset bench-coverage
for binary in tess_bench tess_bench_diagnostics; do  # separate profiles
  LLVM_PROFILE_FILE="$PWD/build/bench-coverage/$binary-%m-%p.profraw" \
    "./build/bench-coverage/bench/$binary" --benchmark_min_time=0.001s
  llvm-profdata merge "build/bench-coverage/$binary"-*.profraw \
    -o "build/bench-coverage/$binary.profdata"
  llvm-cov export -summary-only "build/bench-coverage/bench/$binary" \
    -instr-profile "build/bench-coverage/$binary.profdata" \
    > "build/bench-coverage/$binary-export.json"
done
tools/coverage_gaps.py \
  --export build/bench-coverage/tess_bench-export.json \
  --export build/bench-coverage/tess_bench_diagnostics-export.json \
  --include-root include/tess --cmake-lists CMakeLists.txt \
  --known-gaps tools/coverage_known_gaps.json
```

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
  for `key`, `storage`, `block`, `block_pipeline`, `queued`, `path`,
  `topology`, `scheduler`, `residency`, `maintenance`, `persistence`,
  `query`, `spatial`, `parallel`, `ecs`, `render_delta`, `fields`, and
  `diagnostics`
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
