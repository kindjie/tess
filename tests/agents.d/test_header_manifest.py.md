# test_header_manifest.py

- `tests/test_header_manifest.py`: pins exhaustive, exactly-once stability
  classification and prevents stable aggregates from importing experimental
  or implementation-only headers through angle-bracket, include-root quoted,
  or aggregate-relative quoted includes.
