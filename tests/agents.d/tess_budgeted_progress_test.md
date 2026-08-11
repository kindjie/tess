# tess_budgeted_progress_test

- `tess_budgeted_progress_test`: pins the budgeted-progress benchmark harness
  against the design's fake-clock, accounting, artifact, arrival, and capacity
  search contracts. The numbered source comments map cases to design section
  13; keep that mapping authoritative instead of duplicating it here. The
  clock is scripted in integer nanoseconds, so deadline boundaries and pacing
  are deterministic; the suite takes milliseconds in Debug and never sleeps.
  Fail-closed artifact reading is owned by `test_budgeted_artifacts.py`.
