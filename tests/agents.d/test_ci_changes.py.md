# test_ci_changes.py

- `tests/test_ci_changes.py`: pins the required workflow's fail-closed change
  classifier and tier selection. The TSan path set has a drift scan over every
  header that owns a threading primitive, so a new concurrent component cannot
  silently bypass the pull-request TSan gate.
