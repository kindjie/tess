# test_benchmark_changepoint.py

- `tests/test_benchmark_changepoint.py`: pytest coverage for the
  change-point detector (`tools/benchmark_changepoint.py`) —
  insufficient-history and single-spike suppression, sustained-shift
  flagging with the suspect commit range, the absolute materiality
  floor, fingerprint series breaks versus stratum resumption on
  alternating fleets, exclusion of unusable/non-push/non-main
  artifacts, aggregate-row skipping, unit normalization, and report
  rendering.
