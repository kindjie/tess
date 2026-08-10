# test_doc_outputs.py

- `test_doc_outputs.py`: verifies documented example-output fences stay
  synchronized with the stdout of their compiled binaries, including drift,
  missing/unused `source=binary` mappings, and failing binaries.
