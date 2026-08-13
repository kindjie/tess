# test_header_manifest.py

- `tests/test_header_manifest.py`: pins exhaustive, exactly-once stability
  classification and prevents stable aggregates from importing experimental
  or implementation-only headers through angle-bracket, include-root quoted,
  aggregate-relative quoted, continued, noncanonical, or macro-based includes;
  block-commented directives remain ignored and comment markers inside quoted
  or raw literals cannot alter directive parsing.
