# test_check_doxygen_navigation.py

- `tests/test_check_doxygen_navigation.py`: positive and negative coverage for
  the generated Doxygen menu checker. It proves the exact Docs, Learn, and
  Reference destinations resolve at root, `main`, release-line, and exact-RC
  prefixes; rejects a misspelled generated destination; and fails when the
  representative API pages needed to make the check meaningful are absent.
