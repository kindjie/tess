# test_publish_docs_root.py

- `tests/test_publish_docs_root.py`: synthetic publication-tree coverage for
  the stable root copy, exact `/latest/` compatibility redirects, retained
  non-HTML compatibility assets, root-only sitemap URLs, version-tree
  `noindex` metadata, owned-path pruning, and fail-closed collision handling.
  It also pins the API-response filtering used by the lossless publication
  turn: only older, active, non-PR documentation runs can block a publisher.
  The tests use tiny text placeholders for binary assets because the helper
  must copy them byte-for-byte rather than interpret them.
