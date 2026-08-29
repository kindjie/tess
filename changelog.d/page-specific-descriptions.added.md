- Every maintained documentation page now carries its own description
  (issue #287); thirty-one indexable pages had shared the generic site
  fallback, making search snippets and social cards interchangeable.
  A new build-time check reads the BUILT site and fails on a missing,
  duplicated, or generic description -- mkdocs falls back silently, so
  source front matter alone proves nothing about what ships, and the
  built-site check indeed caught two section index pages the source
  sweep had missed.
