# test_pre_push_ranges.py

- `tests/test_pre_push_ranges.py`: real temporary-Git topology coverage for
  new-branch pre-push ranges. It pins destination/namespace validation,
  non-`main` defaults, mixed updates, disconnected and multiple merge bases,
  and the conservative behavior retained for tags and ambiguous evidence.
