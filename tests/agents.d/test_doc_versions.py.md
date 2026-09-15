# test_doc_versions.py

- `tests/test_doc_versions.py`: synthetic development/release version-policy
  cases plus the repository's current release-version consistency gate
  derived from the checkout version and assembled release records.
  README installation examples are optional, but any `find_package` or
  `GIT_TAG` version they include must match the appropriate source or release.
