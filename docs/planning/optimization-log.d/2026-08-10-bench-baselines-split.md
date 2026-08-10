## 2026-08-10 - Baseline collection split out of the gates job

- Area: CI benchmark pipeline, not library code. Recorded here because
  it creates a data gap trend readers must know about: no baseline
  artifacts reached the history/change-point pipeline between
  2026-08-07 (the last completed collection, #107's push run) and this
  change — three days of merged perf work, including the seam-scan
  hoist (#140) and the packed open node (#150), have no CI baseline
  points. Treat change-point verdicts spanning that window
  accordingly.
- Hypothesis: Benchmark Gates' main-push variant was cancelled at the
  45-minute ceiling #109 added — measured step timings put its real
  runtime at ~51 minutes (build 13-21, test ~1, threshold gates 9-10,
  baseline collection ~27), and the long steps are push-only, so
  pull-request CI validated only the ~25-minute short variant. The
  kill always landed in the final, non-gating baseline step with the
  threshold gates green.
- Method: a dedicated main-push-only `bench-baselines` job (own ccache
  key family, gates family as warm fallback) collects and uploads the
  baselines; it stays outside `ci-gate` by design and reports through
  the ci-failure issue via step-level timeouts (a job-ceiling kill
  concludes `cancelled`, which the reporter ignores because benign
  concurrency supersedes conclude the same way). Both artifact
  consumers re-point at the new job. Policy tests pin the job guard,
  the overrun-reporting contract, and the artifact wiring.
- Evidence: accepted. Three timeout-cancelled runs with the identical
  signature (#148, #149, #150 pushes) against the ~51-minute measured
  total; the gates job's worst path is now ~32 minutes under its
  45-minute ceiling, and the baselines job's ~49-minute worst sits
  under step budgets of 30 + 35 with a 75-minute backstop.
- Follow-ups: the post-merge main-push run is the real verification —
  first expected green full CI since 2026-08-07 — and the first
  baseline artifacts close the data gap from that run onward.
