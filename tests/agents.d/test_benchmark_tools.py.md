# test_benchmark_tools.py

- `tests/test_benchmark_tools.py`: pytest coverage for the benchmark gating
  tools (run with the pinned `requirements-dev.txt` environment, and in the CI
  hooks-backstop job alongside `tests/test_git_hooks.py`). Verifies
  `tools/benchmark_thresholds.py` rejects duplicate benchmark names,
  unthresholded results, empty result sets, and unknown limit keys; selects
  repetition aggregates (median default, `--aggregate` override), converts
  all four Google Benchmark time units, reports unsupported units without a
  traceback, fails on missing benchmarks, permits only explicitly named
  feature-disabled results to be absent, rejects entries with no enabled limit
  unless they explicitly set `gating: false`, and reports missing/malformed
  input files as clear errors;
  every literal benchmark name in a threshold-gated family also has an entry,
  including the block-pipeline, maintenance, persistence, query, and spatial
  manifests and their CI/baseline wiring;
  that `tools/benchmark_baseline_summary.py` filters aggregates by
  `run_type` and quotes CSV fields; that `tools/benchmark_trends.py` reads
  every result file in a baseline artifact, errors on unmatched
  `--benchmark` selectors, and plots each benchmark on the metric its
  family gates (real time where `max_cpu_time_ns` is null, CPU time
  otherwise); and that `tools/benchmark_artifact_metadata.py`
  writes the expected metadata fields. Literal-name extraction covers
  multiline adjacent C++ string literals.
- The two metric cases give `real_time` and `cpu_time` deliberately
  different values, so a tool that reads the wrong field fails rather
  than coincidentally agreeing. The `entry` helper still defaults
  `real_time` to `cpu_time`, which is why every other case here is
  indifferent to the change.
- The renderer's two fail-closed cases assert the specific message,
  not just the exit code. A missing directory and a directory naming
  no benchmarks both exit 1, but only the message distinguishes them:
  asserting the code alone let a mutant that deleted the missing
  directory check survive, because the shared loader then failed for
  the other reason and the test still passed.
