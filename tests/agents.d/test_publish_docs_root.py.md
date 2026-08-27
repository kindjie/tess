# test_publish_docs_root.py

- `tests/test_publish_docs_root.py`: synthetic publication-tree coverage for
  the stable root copy, exact `/latest/` compatibility redirects, retained
  non-HTML compatibility assets, root-only sitemap URLs, version-tree
  `noindex` metadata, owned-path pruning, and fail-closed collision handling.
  It also pins the API-response filtering used by the lossless publication
  turn: polling is bounded to active statuses, and actual attempt start time
  orders normal runs and reruns while pull requests remain excluded. The
  bootstrap recognizes only the exact known legacy root robots file, and a
  non-stable sync preserves the manifest-owned replacement.
  The tests use tiny text placeholders for binary assets because the helper
  must copy them byte-for-byte rather than interpret them.
