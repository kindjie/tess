- Changelog entries are now written as per-change fragments under
  `changelog.d/` and `docs/decisions/changelog.d/`, assembled into the
  maintained changelogs at release by `tools/assemble_changelog.py`.
  Concurrent branches no longer conflict on a shared changelog file.
