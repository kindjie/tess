## 2026-08-10 - Per-test documentation moves to agents.d fragments

- Changed: the per-test catalogue moves from one shared `tests/AGENTS.md`
  file into per-test fragments under `tests/agents.d/` (one `<name>.md`
  per GoogleTest target and per pytest file); `tests/AGENTS.md` keeps only
  the cross-cutting conventions and points at the fragments.
- Why: the shared file sat at 23,926 of the 24,000-token file limit while
  the drift gate required every new test target to be added to it — one or
  two more tests and any test-adding branch fails CI with no legal move.
  It was also the repository's largest merge-conflict surface (166 commits
  in the 90 days before the split), the same shared-append-file pathology
  the changelog and optimization-log fragment directories already solved.
- Decided: the drift gate becomes an exact bidirectional mirror
  (`agents_fragment_issues` in `tools/git_hooks.py`). Every
  `add_executable(tess_*)` target and every `tests/test_*.py` file needs a
  named fragment — the old regex only saw CMake targets, so nine pytest
  suites had silently accumulated with no entry — and an orphan fragment,
  an empty body, or a mismatched `# <name>` heading fails, so a renamed or
  removed test cannot leave stale documentation and an empty placeholder
  cannot satisfy the gate.
- Fragment content at migration is the old catalogue entry verbatim; a
  deferred editorial pass (design doc, reviewed 2026-08-10) may later trim
  enumeration that duplicates test sources, gated on review before any
  trim lands.
