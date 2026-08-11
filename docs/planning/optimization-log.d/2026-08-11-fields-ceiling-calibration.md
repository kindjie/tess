## 2026-08-11 - Calibrate the five post-fix fields ceilings

- Area: the five fields gates deferred after the chunk-level capture fix.
- Evidence: ten distinct successful main-push baseline artifacts, enumerated
  in the benchmark calibration history, supplied 100 raw samples per benchmark
  under usable Ubuntu 24.04 runner fingerprints.
- Decision: accepted. Applying the standing 2x maximum-observed rule tightens
  `goalset_build_1` to 326,214 ns, `goalset_build_16` to 341,301 ns,
  `goalset_build_256` to 361,106 ns, `cache_miss_store` to 352,727 ns, and
  `cache_eviction` to 357,455 ns. These are calibrated gates rather than
  bootstrap evidence.
- Scope: other explicitly labeled fields bootstrap cells remain advisory.
  Their evidence windows and workloads are independent of this five-cell
  deferral and were not silently promoted by the recalibration.
