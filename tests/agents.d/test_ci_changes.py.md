# test_ci_changes.py

- `tests/test_ci_changes.py`: pins the required workflow's fail-closed change
  classifier and tier selection. The TSan path set has a drift scan over every
  library header and test that owns a threading primitive, so a new concurrent
  component cannot silently bypass the pull-request TSan gate.
- The change-point dispatch assertion keeps every known verdict explicit:
  partial coverage warns and exits before issue commands, while an empty or
  unknown verdict reaches the non-zero catch-all.
