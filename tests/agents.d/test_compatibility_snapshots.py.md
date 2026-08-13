# test_compatibility_snapshots.py

- `tests/test_compatibility_snapshots.py`: validates stable header, aggregate,
  and public-name inventories; immutable consumer/archive metadata; required
  release snapshots; and release-tag immutability. Source compatibility beyond
  names is exercised by compiled consumer fixtures rather than parsed in
  Python.
