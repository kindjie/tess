# test_doxygen_integration.py

- `tests/test_doxygen_integration.py`: contracts for the explicit bridge
  between authored pages and the generated C++ reference. It pins supported
  version-relative Doxygen return tabs at root, `main`, release-line, and
  exact-RC prefixes; the curated API landing-page surfaces; compact API-used
  summaries on existing learning pages; exact same-origin generated pages and
  anchors; the Doxygen layout's 1.17 minimum-version contract; and the
  deliberate absence of an autolinker or unified search.
