# test_check_mermaid.py

- `tests/test_check_mermaid.py`: pins Mermaid fence extraction, browser
  validation, runtime fetching, digest verification, and repository-wide live
  diagrams. A Mermaid example nested in a wider fence is not a live diagram;
  missing completion markers and unterminated fences fail closed. A digest
  mismatch must write nothing.
