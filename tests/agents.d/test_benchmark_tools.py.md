# test_benchmark_tools.py

- `tests/test_benchmark_tools.py`: pins threshold gating, manifest coverage,
  baseline summaries, trend rendering, artifact metadata, and fail-closed
  inputs for the benchmark tools. Every literal name in a gated family must
  have a manifest entry and CI/baseline wiring; extraction includes adjacent
  multiline C++ string literals.
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
