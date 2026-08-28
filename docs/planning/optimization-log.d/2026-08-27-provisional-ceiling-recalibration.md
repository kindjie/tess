## 2026-08-27 - Two provisional 2x ceilings recalibrated to the file's own bootstrap convention

**Question:** `path/weighted_batch_planner_100_neargoal_open_512x512`
cancelled two default-branch runs. Is that a regression, or is the
ceiling wrong for the environment it runs in?

**Answer: the ceiling was wrong.** The benchmark's work counters are
identical across passing and failing CI runs -- `avg_expanded_nodes=20.2`,
`expanded_total=2.02k`, `cost_total=1.92k`, `field_builds=2` -- so the
algorithm did the same work each time and only elapsed time moved. That
rules out an algorithmic regression without needing a bisect.

**Evidence.** Local median 53,316 ns at 0.32% CV over ten repetitions on
Apple M3 Max, essentially the 50,096 ns the ceiling was derived from in
July. The decisive number is from the environment that failed: four
recorded `benchmark-baselines-*` artifacts from main runs put this
benchmark's *median* between 68,331 and 101,147 ns on hosted runners,
against a ceiling of 101,000 ns. A gate calibrated below the median of
its own environment fails on noise by construction and cannot be fixed
by rerunning.

**Decision: accept.** Both remaining provisional 2x entries in
`bench/thresholds/path.json` move to the 6x bootstrap over a fresh local
arm64 median that the other 89 thresholds in that file already use
(101,000 to 320,000 ns; 182,000,000 to 358,000,000 ns). This applies an
existing rule to the two entries that predated it rather than inventing
a number to clear a failure.

**Not addressed.** The same file still carries seven explicitly
uncalibrated `BOOTSTRAP 4x` entries, each recording its own retry
condition ("recalibrate at 2x the max over ten CI baselines"). Those are
labelled as provisional in the manifest and are untouched here.

**Retry conditions:** recalibrate from CI baselines rather than a local
median once ten post-change main runs have accumulated for these two
benchmarks, which is the treatment the file's other bootstrap entries
name for themselves.
