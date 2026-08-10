# test_workload_matrix.py

- `tests/test_workload_matrix.py`: pytest coverage for the benchmark
  workload-matrix drift checker (`tools/check_workload_matrix.py`) and
  the shipped catalog (`bench/workload-matrix.json`). It pins
  exactly-one-rule classification (unclassified and doubly matched
  names fail), dead-rule detection, capture/default/override
  precedence, value-based dimension-token consumption (an extent or
  executor token in a name must AGREE with the generated cell —
  partial captures and contradicting defaults fail, including
  pool-kind-without-width), rule-shape validation (anchoring
  required, bad regexes and out-of-range captures report instead of
  raising, override keys that match nothing are stale),
  unknown-versus-not_applicable dimension values, vocabulary
  enforcement for cells AND selectors (a selector typo can never
  silently disable retirement), structured unmeasured selectors
  (reason required, family-scoped selectors supported, a measured
  cell matching a selector forces retirement, boolean `policy`
  entries exempt, `composite` registrations never retire selectors),
  Google Benchmark control-suffix canonicalization (`threads:N` is
  identity-bearing and kept), the three universe sources (threshold
  manifests, `lab/` source literals, runtime listing files), nonzero
  exit on findings, and the shipped catalog's coherence with the
  static universe (manifests plus lab literals; the compiled
  registration union is checked in the bench CI job).
