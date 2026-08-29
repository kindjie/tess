# test_git_hooks.py

- `tests/test_git_hooks.py`: broad coverage for the local hooks and CI
  backstops, including privacy, token limits, indexed content, pre-push path
  classification and labels, dependency locks, and workflow pinning. New
  branch topology and destination validation live in
  `tests/test_pre_push_ranges.py`. The privileged
  CI recovery workflow is the sole action-free workflow; all invoked actions
  remain GitHub-owned and SHA-pinned. The
  diff-scoped clang-tidy timeout is pinned to its large-surface budget. The
  `tests/agents.d/` gate is an exact bidirectional mirror: missing or orphaned
  fragments, empty bodies, and byte-inexact headings all fail against the real
  tree. The Dev example-smoke count must match the literal executable targets
  declared by the examples build. The hook-backstop invocation is matched to
  the `tests/test_*.py` glob in both directions, so a new suite cannot
  disappear from CI's enumerated command.
- Release workflow coverage pins an empty CMake compiler launcher on every
  ccache-free job, bounds the three-core Xcode floor build, and requires Conan
  creation in C++20 mode. Release-floor coverage pins the Visual Studio 2022
  runner and requires the MSVC metadata lookup and 19.44 match to fail closed.
  The non-PR failure reporter directly observes every applicable workflow job.
- Release-evidence coverage requires checksummed, retained job logs containing
  actual versions, a provenance workflow-run URL, and explicit expected/pinned
  toolchain identifiers.
- Documentation workflow coverage stamps the selected Doxygen label,
  normalizes the selector before root assembly, and uploads the checked source
  worktree rather than an adjacent unprepared tree.
- Failure-reporter coverage executes the supersession classifier embedded in
  the workflow: a derivative gate-only failure is silent only when a newer
  push run of the same workflow and branch exists. Real job failures,
  timeouts, dispatches, unrelated newer runs, and unconfirmed supersession
  remain reportable.
