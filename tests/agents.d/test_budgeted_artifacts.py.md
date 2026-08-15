# test_budgeted_artifacts.py

- `tests/test_budgeted_artifacts.py`: pins the fail-closed budgeted-progress
  artifact schema, mode-specific fields, percentile publication, and capacity
  bands. Conservation is recomputed from non-negative counters rather than
  trusting the artifact's own `*_identity_ok` flags. Capacity-band edges must
  be tested points with matching verdicts and cannot invert. Movement tiers fail closed: unknown
  `movement_tier` values reject, the field must agree with the tier encoded
  in `scenario_id`, legacy artifacts omitting the field mean baseline, and
  `trace.realized_churn_sha256` must be well-formed hex when present.
