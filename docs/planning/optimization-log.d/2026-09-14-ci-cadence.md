## 2026-09-14 - Separate merge CI from broad periodic campaigns

- **Hypothesis:** repeating the complete analysis and platform matrix after
  every merge spends runner time on overlapping evidence; retaining the PR
  baseline and unconditional main TSan should preserve prompt regression
  feedback while broader campaigns run weekly or on demand.
- **Controlled change:** main pushes select baseline quality presets. Full
  warnings, release, analysis, macOS, compiler floors and calibrated benchmark
  thresholds move to weekly/manual runs. Release identity validation remains
  independent. Advisory benchmark history stays on completed main runs;
  cancellation can still leave gaps between merged commits.
- **Method and evidence:** workflow/classifier/hook regression tests verify
  PR, push, scheduled and manual selection plus exact release identity.
  Local validation passed 382 Python checks. The pre-push CTest run passed
  with no failures across 1,645 discovered cases in 146.70 seconds (one
  platform-inapplicable case skipped). These establish correctness, not a
  controlled hosted speedup. Hosted job evidence is retained on
  [PR #312](https://github.com/kindjie/tess/pull/312).
- **Result and decision:** accept removing the duplicate broad matrix from
  each main push. PR gates, main TSan and baseline collection remain. Detection
  of macOS, floor-toolchain and full-analysis regressions may now take until
  the weekly run; dispatch those checks for affected changes before merging.
- **Deferred:** measure comparable completed main runs before claiming a
  runtime or runner-cost percentage. Reconsider cadence when a periodic job
  finds regressions missed by the retained baseline. Closing cancellation
  gaps in benchmark history is separate work; this change does not promise
  evidence for every merged SHA.
