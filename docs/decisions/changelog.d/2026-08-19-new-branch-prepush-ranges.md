## 2026-08-19 - Verified new branches use local pre-push ranges

- Changed: qualify the 2026-07-30 rule that all new refs run the full local
  suite. A new remote branch may now use path-based selection when Git supplies
  exactly the configured destination remote and one of its effective push
  URLs, the remote's symbolic default stays inside its local tracking
  namespace, and that default and the pushed tip have exactly one merge base.
  New tags and other new refs, missing or mismatched destination evidence,
  cross-remote defaults, and ambiguous or disconnected histories still fail
  open to the full suite.
- Reason: the all-zero remote object identifies every first branch push as an
  unresolvable range even when the repository has an offline heuristic in its
  locally tracked remote default. That made trivial first pushes pay for the
  full suite and weakened the intended benefit of tested path classification.
- Authority: the heuristic does not know or certify a pull request's base.
  Ordinary behind-staleness can select extra work; rewritten or changed
  defaults and non-linear history can instead narrow the local evidence. CI
  remains authoritative, and `TESS_PREPUSH_FULL=1` remains the explicit local
  full-cycle escape hatch.
- Affected docs: `docs/git-hooks.md`, `tests/agents.d/`.
- Affected code: `tools/git_hooks.py`, pre-push topology tests, CI pytest
  inventory.
