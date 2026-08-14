# test_benchmark_changepoint.py

- `tests/test_benchmark_changepoint.py`: pins change-point detection,
  stratification, artifact selection, normalization, materiality, suspect
  ranges, and reporting.
- Coverage verdicts use the raw-name union of the newest three same-stratum
  artifacts. Cases distinguish missing candidates, unusable selected metrics,
  seven versus eight baseline-artifact medians, partial coverage, and a
  suspect that wins while preserving its coverage gaps. Operational verdicts
  keep the same coverage fields and precede benchmark-level reduction.
- Metric selection is pinned in both directions: a real-time-gated
  benchmark flags on a real-time shift and ignores CPU-time drift, a
  CPU-gated one keeps the old behaviour, and a benchmark absent from
  the manifests defaults to CPU time. The `_artifact` helper writes
  `real_time` equal to `cpu_time` unless a case sets them apart, so
  metric-agnostic cases stay unaffected by that distinction. The CLI
  cases pin the wiring rather than the rule: that the default
  `--thresholds-dir` resolves to the repository manifests (the CI job
  passes none), and that a missing directory exits non-zero instead of
  silently defaulting the suite to CPU time.
- `newest-unusable` is built as its own result rather than overwriting an
  older analysis; its CLI case rejects stale suspect-range fields.
- Two cases pin fail-closed loading, both from a reviewer probe: a
  thresholds directory with no manifests, and a manifest naming no
  benchmarks. Either would otherwise produce an empty metric map,
  which reads as "every benchmark defaults to CPU time" and would
  reinstate the defect this change removes while every test still
  passed. The CLI case additionally pins that the failure is reported
  rather than raised as a traceback.
