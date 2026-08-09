- The CI gate inventory in `CONTRIBUTING.md` now records which tier runs
  each check — pull request, pull request when the change classifier
  selects it, main-only, or advisory. The list previously read as though
  a pull request were checked under TSan, release, macOS and full-tree
  clang-tidy, none of which run there. Tiers were read from
  `.github/workflows/ci.yml` and `tools/ci_changes.py` rather than from
  the surrounding prose. The exception-free contract jobs, which the
  inventory omitted entirely, are now listed, and CodeQL is recorded as
  deliberately advisory.
