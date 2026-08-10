# test_ci_changes.py

- `tests/test_ci_changes.py`: pytest coverage for the required CI workflow's
  fail-closed change classifier. It pins the narrow documentation path
  allowlist, empty and mixed change behavior, full nonzero revision validation,
  NUL-delimited Git output, disabled rename detection, Git-error fallback, and
  the job-output command-line contract; for the tiered topology it pins the
  concurrency-sensitive path set behind the pull-request TSan gate (with a
  drift scan asserting every header owning a threading primitive is
  classified sensitive), TSan fail-closed behavior, and per-event
  quality-preset selection.
