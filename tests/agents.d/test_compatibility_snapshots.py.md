# test_compatibility_snapshots.py

- `tests/test_compatibility_snapshots.py`: validates stable header, aggregate,
  and per-header public-name inventories; canonical CMake consumer contracts;
  immutable consumer/archive metadata and portable paths; required release
  snapshots; and release-tag immutability. Source compatibility beyond names
  is exercised by compiled consumer fixtures rather than parsed in Python.
