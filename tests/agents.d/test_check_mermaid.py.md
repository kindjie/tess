# test_check_mermaid.py

- `tests/test_check_mermaid.py`: pytest coverage for the Mermaid fence
  checker (`tools/check_mermaid.py`) and the pinned-runtime fetcher
  (`tools/fetch_mermaid.py`). It pins fence extraction with reported line
  numbers, the SuperFences variants the site allows (up to three leading
  spaces, tilde fences, four-or-more delimiters, `{.mermaid #id}`
  attribute openers), mermaid examples nested inside a wider enclosing
  fence being skipped and other languages ignored, unterminated fences
  reported rather than swallowed, `</script>` escaping in the generated
  browser harness, result parsing that fails closed when the completion
  marker is missing and unescapes DOM entities, the documentation-tree
  walk, and the fetcher's SHA-256 verification, tarball member
  extraction, and refusal to write anything when a download does not
  match the pinned digest. A live case asserts every diagram under
  `docs/` still extracts, non-empty.
