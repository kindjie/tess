# test_git_hooks.py

- `tests/test_git_hooks.py`: broad coverage for the local hooks and CI
  backstops, including privacy, token limits, indexed content, pre-push test
  selection, labels, dependency locks, and workflow pinning. The
  `tests/agents.d/` gate is an exact bidirectional mirror: missing or orphaned
  fragments, empty bodies, and byte-inexact headings all fail against the real
  tree. The hook-backstop invocation is matched to the `tests/test_*.py` glob
  in both directions, so a new suite cannot disappear from CI's enumerated
  command.
