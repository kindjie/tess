- The two remaining provisional 2x entries in
  `bench/thresholds/path.json` are recalibrated to that manifest's own
  6x bootstrap convention over a fresh local arm64 median. One of them
  had been cancelling default-branch runs. Its 2x ceiling of 101,000 ns
  sat *below* the gate's own recorded CI medians, which span 68,331 to
  101,147 ns, so the gate could fail on work counters identical to a
  passing run. No benchmark regressed: the local median is 53,316 ns at
  0.32% variation, essentially unchanged from the 50,096 ns the original
  ceiling was derived from. The file's seven explicitly uncalibrated
  `BOOTSTRAP 4x` entries are unaffected and stay provisional.
