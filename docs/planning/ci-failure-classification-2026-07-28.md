# CI failure classification, 2026-07-28

Status: point-in-time record. This is the phase 1 re-derivation of the
failure classification required by the
[testing and benchmarking redesign](test-and-benchmark-redesign.md)
(section 10, phase 1), which supersedes that document's section 2
snapshots (28 failed runs on 2026-07-23, 38 on 2026-07-25). It exists to
settle two questions before the CI re-tier: whether the jobs the redesign
demotes to main have ever fired independently on a pull request, and
whether cppcheck — one classified real catch, nine unclassified failing
appearances — keeps its blocking seat.

## Method

All failed workflow runs were pulled from the GitHub Actions API on
2026-07-28 (46 runs, all workflows, full repository history), then every
failed job and its failing step per run (126 failed jobs). Ambiguous jobs
were classified from their logs individually. Counts include runs created
through 2026-07-29 UTC.

By workflow: CI 34, Documentation 9, Copilot review 3 (external app,
excluded below). Documentation failures (docs link checker, Doxygen
install, WebAssembly smoke) belong to the independent Documentation
workflow and do not bear on the CI re-tier.

A job is counted *isolated* when it is the only failed job in its run
after excluding `CI Gate`, which is a derivative aggregate of the other
jobs' results and fails whenever any of them does.

## Failed-job frequency, CI workflow

| Failures | Job | Isolated |
| --- | --- | --- |
| 19 | Quality Gates (dev-clang-tidy) | 5 (2 push to main, 3 PR) |
| 16 | Benchmark Gates | 3 |
| 10 | Quality Gates (dev-asan) | 2 |
| 10 | Quality Gates (dev-cppcheck) | 1 |
| 9 | Windows MSVC | 4 (2 PR test, 2 main build) |
| 6 | Quality Gates (dev-werror) | 0 |
| 5 | Dev Build And Tests | 0 |
| 5 | Quality Gates (release) | 0 |
| 5 | Quality Gates (dev-tsan) | 0 |
| 4 | Hook Backstop Checks | 2 |
| 3 | macOS Build And Tests (each preset) | 0 |
| 3 | GCC Compile | 0 |

Excluded from the table: 9 `CI Gate` failures (derivative aggregate) and
6 failures of the advisory-workflow jobs (`Windows MSVC (advisory)`,
`Advisory Checks (dev-clang-tidy-advisory)`), which gate nothing.

Non-isolated appearances split among universal breaks — runs where a
broken tree failed 10-14 jobs at once — multi-cause runs where 2-5 jobs
failed concurrently for unrelated reasons (2026-07-07, 2026-07-23), and
small paired failures (for example the 2026-07-12 Windows-test pairs).

The redesign's demotion premise is re-verified on the full window:
**dev-tsan, dev-werror, release, and both macOS jobs have never been the
sole failing job of any run** in repository history — no PR or main
failure has ever been attributable to one of them alone. GCC Compile has
also never fired in isolation; it stays a PR gate per the redesign's
section 5 on cost grounds (~1 minute warm, and it is the only gate
exercising GCC's stricter two-phase lookup).

Benchmark Gates' three isolated firings: two 2026-06-08 threshold
scaffold failures from the gate's bring-up era, and the 2026-07-21
dependabot false positive (0.66% over ceiling, passed on rerun) already
recorded in the redesign document. The 2026-07-23 confirmed catch
appears in this window as part of the roadmap-completion iteration runs.

## The ten cppcheck failures, individually

| Run | Date | Context | Classification |
| --- | --- | --- | --- |
| 29124438281 | 07-10 | PR, sole failed job | **Real catch**: `incorrectStringBooleanError`, always-true string-literal assertion in `sim/auto_exec.h` (previously classified) |
| 28886509199 | 07-07 | main, 10-job break | Infra: CMake compiler smoke test failed at configure (exit 127); no cppcheck signal |
| 28886843637 | 07-07 | main, 10-job break | **False positive, class 1**: parser `syntaxError` on `tests/tess_allocation_counter_test.cc:29`, a gtest-macro-heavy file cppcheck misparses |
| 28887333600 | 07-07 | main, 5-job multi-cause | Same `syntaxError` false positive, second appearance |
| 28888943908 | 07-07 | main, 3-job | **False positive, class 2**: `arithOperationsOnVoidPointer` on `block/block.h` — cppcheck misparses `std::byte*` as `void*`; suppressed at config level in 08e136f |
| 28890416527 | 07-07 | main, 2-job | Same `std::byte*` false positive, second appearance |
| 29988992157 | 07-23 | PR, 14-job break | **False positive, class 3**: `danglingLifetime` on `persistence/archive.h` `save_world_archive` — flagged a construct the final code shows was rewritten to appease the analyzer (e8b1cc4, "Fix persistence toolchain portability") |
| 30034878103 | 07-23 | PR, 14-job break | Same `danglingLifetime` false positive, second appearance |
| 30037315812 | 07-23 | PR, 13-job break | Same, third appearance |
| 30039020740 | 07-23 | PR, 4-job | **Tool crash**: "Error running cppcheck" with no diagnostic while checking `tests/tess_shape_test.cc` — cppcheck 2.21's template simplifier crashing on a valid test TU |

Independent record: one real catch, seven false-positive appearances
across three classes, one tool crash, one infrastructure echo. The three
`danglingLifetime` appearances arrived inside universal breaks, but the
diagnostic itself was cppcheck's own false positive, resolved by
rewriting valid code — they count against the tool, not as echoes.

How the false positives were actually resolved matters to the verdict:

- Class 2 (`std::byte*`) has a standing config-level suppression.
- Class 1 (`syntaxError`) is deliberately **not** suppressed — a
  regression test (`test_cppcheck_smoke_does_not_suppress_syntax_errors`)
  enforces that — because suppressing parse errors would blind the check.
- The durable mitigation was a **scope reduction**: since #59
  (2026-07-24), cppcheck analyzes only the `tess_smoke` umbrella-header
  TU, because cppcheck 2.21's template simplifier crashes on several
  valid template-heavy test TUs (the tool-crash row above). Test and
  benchmark TUs are no longer analyzed; compiler and clang-tidy gates
  retain per-instantiation coverage there. The apparent quiet since
  07-23 partly reflects this narrower scope, not only tool convergence.

## Verdict: cppcheck keeps its blocking PR seat, narrowly

The redesign's retention rule was "one real catch and nine unclassified
failures — classify before deciding." Classified, the case is thinner
than the redesign assumed but still holds:

- The one real catch was the sole failing job on its PR — exactly the
  isolated, actionable signal a blocking gate exists to give. All seven
  false-positive appearances predate the #59 scope reduction. One class
  cannot recur under it (the parser `syntaxError` fired on test TUs that
  are no longer analyzed); the `std::byte*` class is held off by its
  config suppression and the `danglingLifetime` class by the source
  rewrite, both of which the narrowed scope still exercises.
- Cost: measured over the 10 successful CI runs preceding 2026-07-28,
  post-scope-reduction cppcheck has a ~3-minute median against Windows
  MSVC's ~8.5 — well inside the retained floor. Before the reduction it
  ran 8.8-9.2 minutes, exceeding the Windows job in at least two
  2026-07-23 runs (the full-tree clang-tidy ran longer still), so the
  "prices no critical path" argument is true only of the current,
  narrowed configuration.
- The counterweight: what remains analyzed is one umbrella TU, so the
  seat is cheap partly because the check now does less. That trade was
  made deliberately in #59 and is enforced by tests; this document just
  records that the blocking seat covers the narrowed scope, not the
  original one.

Revisit if the crash class recurs under the smoke-only scope, if a new
false-positive class appears, or if a cppcheck upgrade allows restoring
the analyzed-TU set (per the retry note in `TessProjectOptions.cmake`).

## Consequences for the re-tier

- Demote from the blocking PR set: dev-werror, release, both macOS
  jobs, and the full-tree clang-tidy sweep (a diff-scoped clang-tidy job
  stays blocking; its 3 real catches and isolated firings say the check
  itself earns a seat — only the full-tree run's latency does not).
  dev-tsan becomes conditional: path-filtered on PRs when
  concurrency-sensitive files change, unconditional on main.
- Keep blocking on PRs: dev build+tests, GCC compile, hook backstop,
  ASan, Windows MSVC, cppcheck, bench compile+smoke.
- Benchmark threshold gating and baseline artifact collection move to
  the main tier per section 5 of the redesign, unretired pending its
  phase 4 shadow-mode criteria.
