# test_check_public_surface.py

- `tests/test_check_public_surface.py`: pytest coverage for the
  public-surface manifest gate (`tools/check_public_surface.py`, a required
  CI check since 2026-07-07, run in
  the same CI hooks-backstop pytest invocation). Synthetic header fixtures
  verify type and free-function extraction at namespace scope, skipping of
  members, function-local declarations, comments, macro-body braces, and
  `detail`/`internal` namespaces, plus failure messages for undocumented
  symbols, missing headers, missing mapped documents, and symbols absent from
  their assigned document. One test asserts the committed
  `docs/architecture/surface.json` stays complete against the real
  `TESS_PUBLIC_HEADERS` headers.
