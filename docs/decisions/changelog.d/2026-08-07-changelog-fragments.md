## 2026-08-07 - Changelog entries move to per-change fragments

- Changed: `CHANGELOG.md` and `docs/decisions/CHANGELOG.md` are assembled
  from fragment files rather than edited directly. Every branch that edits
  a shared changelog conflicts with every other such branch, so a stack of
  N pull requests costs on the order of N² resolutions. Measured on the
  2026-08-07 audit stack: eight conflict resolutions across seven pull
  requests, all in the same two files, none of them related to the code
  under review.
- Rationale beyond the time cost: a changelog conflict is unusually easy
  to mis-resolve *silently*. One resolution in that stack had an empty
  incoming side, where a mechanical keep-both would have deleted entries
  that had just merged, and nothing would have failed. Fragments make the
  common case a no-op instead of a judgement call.
- Shape: release fragments are `<slug>.<category>.md` holding complete
  markdown list items, so assembly is concatenation and the reviewed text
  is the shipped text. Decision fragments are `<YYYY-MM-DD>-<slug>.md`
  holding a complete dated section, ordered newest first by filename.
- Decided: assembly MERGES the existing `Unreleased` body with the
  fragments by category rather than stacking them. Entries written before
  this change still sit under `Unreleased`, and appending a second set of
  `### Fixed` headings under one release would be malformed. Merging keeps
  one heading per category through the transition, after which the
  `Unreleased` body is empty and the merge is a no-op. An end-to-end dry
  run against the real changelogs is what surfaced both that and a stray
  blank line; neither was visible from the unit tests alone.
- Assembly is all-or-nothing: an invalid fragment aborts before anything
  is written or deleted, so a bad fragment cannot half-apply a release.
  `--check` runs in the hook-backstop tier so that failure lands on the
  pull request that introduced it rather than on the release.
