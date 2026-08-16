# test_git_hooks.py

- `tests/test_git_hooks.py`: broad coverage for the local hooks and CI
  backstops, including privacy, token limits, indexed content, pre-push test
  selection, labels, dependency locks, and workflow pinning. The
  diff-scoped clang-tidy timeout is pinned to its large-surface budget. The
  `tests/agents.d/` gate is an exact bidirectional mirror: missing or orphaned
  fragments, empty bodies, and byte-inexact headings all fail against the real
  tree. The hook-backstop invocation is matched to the `tests/test_*.py` glob
  in both directions, so a new suite cannot disappear from CI's enumerated
  command.
- Release workflow coverage pins an empty CMake compiler launcher on every
  ccache-free job, bounds the three-core Xcode floor build, and requires Conan
  creation in C++20 mode. Release-floor coverage pins the Visual Studio 2022
  runner and requires the MSVC metadata lookup and 19.44 match to fail closed.
  The non-PR failure reporter directly observes every applicable workflow job.
- Release-evidence coverage requires checksummed, retained job logs containing
  actual versions, a provenance workflow-run URL, and explicit expected/pinned
  toolchain identifiers.
- Failure-reporter coverage executes both classifiers embedded in the
  workflow: a derivative gate-only failure is silent only when a newer push
  run of the same workflow and branch exists. Real job failures, timeouts,
  dispatches, unrelated newer runs, and unconfirmed supersession remain
  reportable.
