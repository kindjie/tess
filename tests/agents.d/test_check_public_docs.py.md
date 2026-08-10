# test_check_public_docs.py

- `tests/test_check_public_docs.py`: pytest coverage for the dependency-free,
  all-public-header Doxygen comment gate (`tools/check_public_docs.py`).
  Synthetic fixtures verify block and line comments across templates,
  overload-aware coverage, conditional-definition and redeclaration
  deduplication, actionable missing documentation and missing-file failures,
  and exclusion of `detail` symbols.
  One test gates the explicitly opted-in public headers; it does not claim
  member-level or full-API documentation coverage.
