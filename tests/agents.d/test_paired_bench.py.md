# test_paired_bench.py

- `tests/test_paired_bench.py`: pytest coverage for the paired sentinel
  benchmark runner (`tools/paired_bench.py`) and the sentinel contract in
  `bench/sentinels.json`. It pins the twelve-sentinel definitions against
  the threshold manifests, the confirmed-catch names, the section 4.5
  source-map completeness and its coherence with the perf classifier
  (mapped areas are exactly the perf-sensitive ones), benchmark-JSON
  parsing and fail-closed missing-sentinel behavior, ABBA round order,
  the paired per-round-ratio bootstrap (flagging, noise, materiality
  floor, determinism, pairing sensitivity), Bonferroni adjustment,
  verdict combination, markdown rendering, end-to-end orchestration
  against fake benchmark binaries in shadow and confirm modes, and the
  suspect-scoped confirmation path (threshold-manifest metric lookup,
  the 64-name cap, unit normalization, end-to-end suspect verdicts).
