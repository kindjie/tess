# test_compatibility_snapshots.py

- `tests/test_compatibility_snapshots.py`: pins earlier-snapshot superset
  checks, declaration-level source compatibility (including conditional
  declarations, access labels, enums, inherited constructors, and C++20
  derived aggregates), release-tag immutability, installed-package consumer
  metadata, and the exact RC1 snapshot requirement. It intentionally uses a
  minimal synthetic repository; compiling consumers and loading archives are
  separate CMake/runtime release gates.
