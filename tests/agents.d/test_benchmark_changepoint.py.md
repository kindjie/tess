# test_benchmark_changepoint.py

- `tests/test_benchmark_changepoint.py`: pytest coverage for the
  change-point detector (`tools/benchmark_changepoint.py`) —
  insufficient-history and single-spike suppression, sustained-shift
  flagging with the suspect commit range, the absolute materiality
  floor, fingerprint series breaks versus stratum resumption on
  alternating fleets, exclusion of unusable/non-push/non-main
  artifacts, aggregate-row skipping, unit normalization, and report
  rendering.
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
