# test_clang_tidy_changed.py

- `tests/test_clang_tidy_changed.py`: pins candidate selection, compilation-
  database handling, synthesized header units, and fail-closed reference
  commands for the diff-scoped clang-tidy runner, including explicit gated
  exclusions for compile-fail fixtures, opt-in fuzz translation units, and
  platform-only hosts.
