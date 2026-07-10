# Audit 2026-07-09 — Remediation Plan

Executes the findings in `audit-2026-07-09.md`. Six workstreams, each a
feature branch off `main`, tests-first, merged sequentially via PR after
review. Finding IDs (H*/M*) refer to the audit report.

| Branch | Findings | Scope |
| --- | --- | --- |
| `fix/audit2-path-caches` | H1 H2 M3 M4 M6 + cache lows | Failure-product and field-product dependency capture; route-cache API contracts |
| `fix/audit2-executors` | H3 M2 M1 + thread lows | Executor dispatch contracts; partial-plan dirty propagation |
| `fix/audit2-topology` | M7 M8 + shape/meta lows | Checked-accessor guards; graph↔shape binding |
| `fix/audit2-diagnostics` | M9 M10 M11 | Alloc-hook null/MSVC fixes; snapshot thread contract |
| `test/audit2-hardening` | M14 M15 + test lows | Flake-proofing, coverage gaps, weak assertions |
| `ci/audit2-infra` | M12 M13 M16 M17 + infra lows | Parallel-bench gate, trends tool, doc drift, name-scrub hook |

## Fix approaches (decisions)

- **H1/H2 (one mechanism):** dependency capture must include the *blocked
  frontier* — every chunk containing a tile that was passability-checked and
  rejected during the search/flood — in addition to reached/path chunks. Any
  edit that could change the outcome must modify a formerly blocked tile
  adjacent to the searched region, whose chunk is then a dependency.
  Belt-and-braces: `ChunkVersionDependencies::is_valid` returns false for an
  empty dependency set, so no product can ever be vacuously valid.
- **H3/M2:** document the single-dispatch, no-reserve-during-dispatch
  contract on `WorkerPoolPhaseExecutor`; add a `TESS_ASSERT`-backed dispatch
  guard flag so nested/concurrent dispatch fails fast in debug instead of
  deadlocking.
- **M1:** the scheduler marks pathing dirty when *any* operation executed,
  including plans aborted mid-way (gate on executed count, not full-plan
  success).
- **M5 (deferred semantic change):** the chunk-portal route product's
  monotone-staircase NoPath is documented as a heuristic, non-authoritative
  tier in this pass; promoting it to an `Indeterminate`-style status is an
  API change deferred to the next minor version.
- **M6:** dense-world identity aliasing is documented as a contract (one
  runtime per world) rather than fingerprinted, matching the cost profile;
  the sparse path already defends itself.
- **M17:** the private-name pattern is added split-string style so the hook
  file never contains the name it bans.

Everything else follows the report's per-finding fix directions. Lows land
with the workstream that owns their files; purely pathological-input lows
(2^63 extents, 2^128 shape products) get cheap guards or documented
non-goals rather than deep rework.

## Order and verification

Merge order: path-caches → executors → topology → diagnostics → hardening →
infra (rebasing each on the previous merge; CHANGELOG entries conflict by
construction and are resolved at rebase time). Every branch: new tests fail
before the fix, full suite + ASan preset green before commit, codex review
before merge (advisor unavailable), CHANGELOG entry, `tests/AGENTS.md`
updated for any new test target.
