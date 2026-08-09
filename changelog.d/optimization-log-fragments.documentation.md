- The optimization log is now assembled from per-experiment fragments in
  `docs/planning/optimization-log.d/` rather than edited in place, the same
  mechanism the changelogs use. Every performance branch appended to the top
  of one file, so concurrent branches conflicted on every rebase, and the
  file is subject to the repository's 24,000-token limit — which it has
  exceeded twice, forcing an archive split each time. Fragments remove both
  problems: a branch adds a file nobody else touches, and the maintained log
  only grows at release.
