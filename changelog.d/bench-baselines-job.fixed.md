- Main-push CI collects its non-gating benchmark baselines in a
  dedicated job instead of inside Benchmark Gates. The gates job had
  been cancelled at its 45-minute ceiling on every full main push
  since 2026-08-07 — always in the final, non-gating baseline step,
  with the threshold gates themselves green — which failed CI Gate and
  stopped baseline artifacts from reaching the trends pipeline. The
  ceiling merged unvalidated because the long steps only run on push,
  never in pull-request CI. Baselines now run in parallel with their
  own budget, stay outside the merge gate by design, and still report
  through the ci-failure issue when they break.
