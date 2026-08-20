# Contributing to tess

Thanks for working on `tess`. This document collects the developer
workflow: presets, hooks, quality gates, and benchmark policy. For
formatting and layout conventions, read [`docs/style.md`](docs/style.md).
Participation in the project is covered by the
[code of conduct](CODE_OF_CONDUCT.md).

## Workflow expectations

- New or modified functionality lands with tests, and the full suite
  passes before merge - no exceptions.
- Changelog entries are written as **fragments**, not by editing either
  changelog directly. See [Changelog fragments](#changelog-fragments).
- Markdown stays near 80 columns; docs separate maintained material
  from the historical TDD archive (see [`docs/README.md`](docs/README.md)).

## Changelog fragments

Both changelogs are assembled from per-change fragment files. Do not edit
`CHANGELOG.md` or `docs/decisions/CHANGELOG.md` by hand except when
cutting a release.

The reason is mechanical: every branch that edits a shared changelog
conflicts with every other such branch, so a stack of N pull requests
costs on the order of N² conflict resolutions — in a file where a
mis-resolution silently deletes someone's entry rather than failing.
Fragments give each change its own path, so branches merge cleanly.

Release-facing change:

```sh
cat > changelog.d/sparse-residency-observations.fixed.md <<'EOF'
- A `DirtyObservation` taken before a sparse chunk was evicted can no
  longer clear a dirty mark made after it was reloaded.
EOF
```

The name is `<slug>.<category>.md`, where category is one of `added`,
`changed`, `deprecated`, `removed`, `fixed`, `security`, `performance`,
or `documentation`. The body is complete markdown list items — assembly
concatenates them under a `### Category` heading, so what you write in
review is what ships.

Design decision:

```sh
cat > docs/decisions/changelog.d/2026-08-07-residency-intervals.md <<'EOF'
## 2026-08-07 - Residency intervals scope dirty observations

- Fixed: ...
EOF
```

The name is `<YYYY-MM-DD>-<slug>.md` and the body opens with a matching
`## <date> - <title>` heading. Assembly orders these newest first.

Check and preview:

```sh
python3 tools/assemble_changelog.py --check
python3 tools/assemble_changelog.py --preview
```

`--check` runs in CI, so a malformed fragment fails on its own pull
request rather than at release time, when it would block the release.

At release, fold everything in and delete the fragments in one step:

```sh
python3 tools/assemble_changelog.py --release 0.13.0 --date 2026-09-01
```

Assembly is all-or-nothing: if any fragment is invalid, nothing is
written and nothing is deleted.

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
rolling `ci-failure` issue. A successful retry closes that issue only when the
bot's unedited report for an earlier attempt of the same run remains the latest
issue activity; ambiguous or human-owned activity is left open.

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

**Profiling protocol** (optional, signal-triggered — never a gate):
when the change classifier flags perf-sensitive paths, the paired
sentinel run returns `advisory` or `regression`, or a
`perf-change-point` issue opens, investigate in escalation order
(redesign section 4.6):

1. **Counters first.** Read the counter-drift table in the check
   summary. If work changed (`expanded_nodes`, retries, merges), the
   diagnosis is algorithmic — the golden diff names the behavior
   change and profiling is usually unnecessary. If intended, update
   the golden in the same PR.
2. **Reproduce paired.** If counters are flat but time moved,
   reproduce locally before investigating — hosted-runner noise is
   not a finding. Build both revisions and run the paired tool; the
   check summary prints the exact command including a complete
   `--suspects=...` option. Suspects from the diagnostics-binary
   families (`diagnostics/`, `ecs/`, `render_delta/`, `fields/`)
   reproduce against `tess_bench_diagnostics` instead of
   `tess_bench`, so build both targets:

   The base build uses explicit flags rather than the `bench-only`
   preset because a base commit may predate the preset (the CI base
   build does the same):

   ```sh
   base_dir=../tess-paired-base
   git worktree add "$base_dir" <base-sha>
   cmake --preset bench-only
   cmake --build build/bench-only --target tess_bench \
     tess_bench_diagnostics
   cmake -S "$base_dir" -B "$base_dir/build/bench-only" \
     -DCMAKE_BUILD_TYPE=Release -DTESS_BUILD_TESTING=OFF \
     -DTESS_BUILD_EXAMPLES=OFF -DTESS_BUILD_BENCHMARKS=ON \
     -DTESS_ENABLE_ENTT=ON -DTESS_ENABLE_FLECS=ON
   cmake --build "$base_dir/build/bench-only" --target tess_bench \
     tess_bench_diagnostics
   python3 tools/paired_bench.py --mode confirm \
     --base-binary "$base_dir/build/bench-only/bench/tess_bench" \
     --head-binary build/bench-only/bench/tess_bench \
     --sentinels bench/sentinels.json --thresholds-dir bench/thresholds \
     "<paste the whole --suspects=... option from the check summary>"
   ```
3. **Profile and diff.** Build both revisions under the
   `bench-profile` preset, capture at matched filters and durations,
   and diff the symbol summaries (see the benchmark plan's profiling
   workflow). Attribute the delta to a symbol or call path before
   concluding anything.
4. **Record.** Accepted, rejected, deferred, and inconclusive
   outcomes all go to the optimization log per its
   existing convention — deferred entries carry their follow-up
   condition, and inconclusive entries stop the next contributor
   from repeating the dead end. Write a fragment under
   `docs/planning/optimization-log.d/` named `<YYYY-MM-DD>-<slug>.md`
   rather than editing `docs/planning/optimization-log.md`, which is
   assembled from those fragments at release; branches that edit it
   directly conflict with each other.

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

CI runs primarily on `ubuntu-24.04` with Clang and covers the following.
Each entry carries the tier that actually runs it, because the list is
otherwise easy to read as "my pull request was checked by all of this":

- **[PR]** runs on every code-affecting pull request, and blocks the merge.
- **[PR when triggered]** runs on a pull request only when the change
  classifier selects it (`tools/ci_changes.py`).
- **[main]** runs on pushes to `main`, the weekly schedule, and manual
  dispatches — *not* on pull requests.
- **[advisory]** runs but never blocks a merge.

- **[PR]** Dev build and unit tests: `cmake --build --preset dev`,
  `ctest --preset dev`
- **[PR]** Installed header file-set drift check: `tess_installed_headers_file_set`
- **[PR]** Installed package smoke test: `tools/install_smoke.sh`
- **[PR]** Hook backstop checks: `tools/git_hooks.py ci` repository hygiene plus
  pytest for the repo tools (`tests/test_git_hooks.py`,
  `tests/test_benchmark_tools.py`, `tests/test_check_public_surface.py`)
  and the bidirectional public-surface manifest gate
  (`tools/check_public_surface.py` against
  `docs/architecture/surface.json`; required since 2026-07-07)
- **[PR]** Installed-header namespace-scope Doxygen gate: `tools/check_public_docs.py`
- **[main]** Warnings-as-errors build and tests: preset `dev-werror`
- **[PR]** ASan/UBSan build and tests (UBSan findings are fatal): preset
  `dev-asan`
- **[PR when triggered]** **[main]** TSan build and tests
  (`TSAN_OPTIONS=halt_on_error=1`): preset `dev-tsan`. On a pull request
  this runs only when `tools/ci_changes.py` classifies the diff as
  concurrency-sensitive; on main it always runs. The preset excludes tests
  labeled `config:tsan-exempt`: these are single-threaded, CPU-heavy checks
  with no race surface. All targets still compile under TSan. The Dev,
  GCC 12/14, ASan, Windows, and coverage gates retain the Traffic Lab's
  512/1,600-tick crowd checkpoints. The required optimized benchmark gate and
  main Release floors own its 2,048 exact route comparisons. Remove the TSan
  exemption if concurrency enters an exempt model or harness.
- **[main]** Release build and tests: preset `release`
- **[main]** macOS build, tests, and install smoke on `macos-15`: presets `dev` and
  `dev-asan` (no benchmark gates there; thresholds are Linux-calibrated)
- **[PR]** Windows MSVC build, tests, and install smoke on `windows-2025`:
  preset `windows-msvc` (required gate since 2026-07-07)
- **[main]** Strict full-tree clang-tidy gate: `cmake --build --preset
  dev-clang-tidy`. Pull requests instead run a diff-scoped clang-tidy job
  (`tools/clang_tidy_changed.py`), which checks only changed lines
- **[PR]** cppcheck gate: `cmake --build --preset dev-cppcheck`
- **[PR]** Exception-free compiler-mode contracts, built and tested with
  exceptions disabled on three toolchains: Clang (ASan/UBSan), GCC
  (warnings-as-errors), and MSVC. Enabled by
  `-DTESS_BUILD_NO_EXCEPTIONS_TESTING=ON`, which only adds targets;
  `tools/check_no_exceptions_manifest.py` compares the exception-mode
  subsystem manifests, and installed-consumer and FetchContent smokes run
  in exception-free mode
- **[advisory]** Weekly clang-tidy profile: preset
  `dev-clang-tidy-advisory`
- **[PR]** Required GCC compile-only portability check: preset
  `dev-werror` built with GCC
- **[PR]** Required libc++ compile-only portability check: preset
  `dev-werror` built with Clang and `-stdlib=libc++`. macOS also builds
  against libc++, but macOS is main-only, so before this cell a
  libc++-specific failure reached main before anyone saw it
- **[PR]** Benchmark build and smoke tests: preset `bench`
- **[main]** Benchmark threshold gates, one per suite (CPU time except parallel wall
  time). Run one suite with
  `cmake --build --preset bench --target tess_bench_<suite>_thresholds`,
  or every suite the way CI does:
  `cmake --build --preset bench --parallel 1 --target
  tess_bench_all_thresholds`. The gated set is derived in
  `bench/CMakeLists.txt` from the threshold targets defined there, so a
  new suite gates itself and no list needs updating here. `--parallel 1`
  is load-bearing: these are timing gates and must not run concurrently
- **[main]** **[advisory]** CI benchmark baseline collection:
  `cmake --build --preset bench --target tess_bench_ci_baselines`
- **[PR]** **[advisory]** CodeQL static analysis for `actions`, `c-cpp`,
  and `python`, plus a weekly scan. It runs on pull requests and pushes
  but is deliberately **not** a required check: it is configured through
  GitHub's default setup, whose query suites update on GitHub's schedule,
  so gating on it would let an upstream suite update block an unrelated
  pull request. Findings surface on the pull request and in the
  repository's Security tab either way, which is the value; making it
  required would add none. Configuration lives in repository settings,
  not in `.github/workflows/`

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
