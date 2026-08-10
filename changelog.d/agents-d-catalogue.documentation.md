- Per-test documentation moves from the single `tests/AGENTS.md` catalogue
  into per-test fragments under `tests/agents.d/`, with the CI drift gate
  now enforcing an exact bidirectional mirror of the test set (including
  the pytest suites the old gate could not see). Benchmark-binary
  conventions move to `bench/AGENTS.md`, and `CLAUDE.md` import shims make
  the agent instructions load in Claude Code sessions.
