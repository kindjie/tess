# test_summarize_budgeted_curves.py

- `tests/test_summarize_budgeted_curves.py`: pins the budgeted-progress
  curve generator (`tools/summarize_budgeted_curves.py`): section 12
  summary rows derive purely from artifact fields (per-frame and
  per-completion arithmetic, suppressed percentiles propagating as
  `insufficient`), arrival and mixed cells land in the demand table
  with wall rates and lag, capacity bands render the
  confirmed/lowest-unstable pair, matrix holes are named in coverage
  notes rather than silently absorbed, `--strict` makes an empty
  directory fatal, and the CLI writes the three CSV files beside its
  Markdown. Trap: the tool must never hold thresholds or measure — it
  only reshapes artifacts, so every expected value in these tests is
  hand-derivable from the fixture JSON. The movement tier joins the sort key,
  coverage-group identity, and demand columns, so tier cohorts never
  fabricate holes across each other.
