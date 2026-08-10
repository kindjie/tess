# test_clang_tidy_changed.py

- `tests/test_clang_tidy_changed.py`: pytest coverage for the diff-scoped
  pull-request clang-tidy runner (`tools/clang_tidy_changed.py`). It pins
  candidate selection (header/source split, bench and webgpu_stub
  exclusions, deduplication), compile-flag extraction from both
  compilation-database entry forms, synthesized header translation units,
  database-backed source commands, absolute-path database indexing, and the
  fail-closed reference-entry requirement.
