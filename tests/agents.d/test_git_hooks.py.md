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
- Release-package workflow coverage pins an empty CMake compiler launcher on
  the ccache-free image and requires Conan creation in C++20 mode.
- Release-evidence coverage requires an immutable workflow-run URL for actual
  version logs plus explicit expected/pinned toolchain identifiers.
