# test_compare_budgeted_passes.py

- `tests/test_compare_budgeted_passes.py`: pytest coverage for the
  timing-versus-counter cross-pass comparator
  (`tools/compare_budgeted_passes.py`). It pins pairing on scenario,
  budget, arrival rate, and pacing with hard-equal trace hashes — a
  differing hash, or a counter artifact with no timing partner, is a hard
  failure rather than a tolerance finding — the saturated
  work-per-completion gate at 1% relative, which applies only when every
  repetition of both passes completed a full pool wrap (below a wrap the
  ratio measures pool-prefix composition, and one long repetition cannot
  stand in for the short ones), the demand-limited tolerances on
  completions, work units, and per-class deadline success, regime
  divergence (one pass stable, the other not) reported under its own
  label instead of failing a tolerance, and `--smoke-report-only`, which
  the CLI test proves suppresses a statistical finding (the pairing and
  trace-identity hard failures are covered through `run_comparison()`
  directly, not through that flag).
