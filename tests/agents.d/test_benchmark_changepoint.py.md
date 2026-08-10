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
- Measurement epochs (`bench/benchmark-epochs.json`) are pinned three
  ways: pre-epoch readings do not seed a post-epoch baseline, an epoch
  masks only the benchmarks its prefix names, and a genuine shift
  after the epoch still flags — the last of these is what keeps an
  epoch from becoming a permanent mute. The first case gives its
  artifacts a second, steady benchmark on purpose: a real artifact
  carries every family, so dropping one family's readings must not
  remove the artifact from the stratum, and a single-benchmark fixture
  would have tested the wrong thing.
- Two cases pin fail-closed loading, both from a reviewer probe: a
  thresholds directory with no manifests, and a manifest naming no
  benchmarks. Either would otherwise produce an empty metric map,
  which reads as "every benchmark defaults to CPU time" and would
  reinstate the defect this change removes while every test still
  passed. The CLI case additionally pins that the failure is reported
  rather than raised as a traceback.
