# test_budgeted_artifacts.py

- `tests/test_budgeted_artifacts.py`: pins the fail-closed budgeted-progress
  artifact schema, mode-specific fields, percentile publication, and capacity
  bands. Conservation is recomputed from non-negative counters rather than
  trusting the artifact's own `*_identity_ok` flags. Capacity-band edges must
  be tested points with matching verdicts and cannot invert.
