# test_budgeted_artifacts.py

- `tests/test_budgeted_artifacts.py`: pytest coverage for the fail-closed
  budgeted-progress artifact validator
  (`tools/check_budgeted_artifacts.py`). It pins the schema,
  suite-version, experiment-kind, and pass-marker gates (an unknown value
  fails closed rather than being ignored), both flow conservation
  identities recomputed from the counters instead of trusted from the
  artifact's own `*_identity_ok` flags, non-negative counters checked
  before any identity arithmetic, the percentile rule in both directions
  (undersampled families must be null, sufficiently sampled ones must
  publish), and the mode rules: saturated cells omit the deadline group
  and classes and settle at zero; demand-limited cells carry both plus a
  boolean `flow_stable`; unpaced cells carry no frame-start lag, and
  paced cells alone carry it and a finite wall rate derivable from its
  sources; cell artifacts carry a null capacity band; detailed counters
  belong to counter-pass artifacts
  only. The capacity-search schema adds band edges that must be tested
  points with matching verdicts and cannot invert.
