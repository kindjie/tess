# AGENTS.md

This repository is intended to be public. Do not add personally identifying,
private, credential-like, or machine-local information to tracked files.

## Repository Visibility

This repository may start private and become public later. Treat all tracked
content as public from the first commit.

Agent runtime context is not repository documentation. Never copy user identity,
local machine paths, private config locations, credentials, account details, or
automatically supplied agent context into tracked repository files.

Refer to other projects by generic terms (for example, "a downstream consumer"
or "the reference consumer") rather than by name. The one exception is an
explicit declared dependency recorded in `docs/dependencies.md`, which may be
named. This keeps private sibling or downstream project names out of this
public-intended repository. When adding dependencies, record public dependency
documentation in `docs/dependencies.md`.

These rules are backstopped -- not fully enforced -- by the pre-commit hook
and a CI check (see `docs/git-hooks.md`; CI intentionally omits the local
identity patterns), so the checklist below still decides. Install the hooks
with `python3 tools/git_hooks.py install`.

Before committing, inspect the exact staged diff for:

- names, personal profiles, email addresses, phone numbers, and private URLs
- local absolute paths outside this repository
- secrets, tokens, keys, certificates, credentials, and auth config
- private infrastructure, account, or organization details

Do not commit if any of those are present.

Keep this file focused on agent instructions and public repository safety.

## Recording Changes

Three fragment streams feed the maintained logs. Do not add entries to
the assembled files (`CHANGELOG.md`, `docs/decisions/CHANGELOG.md`,
`docs/planning/optimization-log.md`) directly: concurrent branches
conflict on them, and the optimization log is subject to the
24,000-token file limit. `tools/assemble_changelog.py --release`
performs the release writes, its `--check` mode validates fragments in
CI, and archive maintenance of the assembled logs remains the one
direct-edit exception.

- Release-facing changes: `changelog.d/<slug>.<category>.md` holding
  complete markdown list items (categories are listed in
  `tools/assemble_changelog.py`).
- Design decisions: `docs/decisions/changelog.d/<YYYY-MM-DD>-<slug>.md`
  holding exactly one `## <YYYY-MM-DD> - Title` section.
- Performance experiments: one fragment per experiment under
  `docs/planning/optimization-log.d/`, named `<YYYY-MM-DD>-<slug>.md` with
  the same single-section shape. For performance work, use an iterative
  optimize/test/benchmark cycle and record accepted, rejected, and deferred
  experiments here rather than in architecture docs.

## TDD Divergence

TDDs in `docs/tdd/` are historical design intent. When implementation
diverges from them, update maintained docs under `docs/architecture/` and
add a design-decision fragment (see Recording Changes); optionally add a
note to the affected TDD. Do not try to keep TDDs as API reference; add
Doxygen comments to public headers as the API stabilizes, then introduce
generated docs when useful.

## Worktrees and Merging

Coding agents work in linked worktrees (`git worktree add`), not the primary
checkout, so concurrent sessions do not disturb each other's state.

Do not run `gh pr merge --delete-branch` from a linked worktree. When the
base branch is checked out in the primary worktree, gh's post-merge local
cleanup fails partway (cli/cli#13380) and has been observed to leave the
repository corrupted: `core.bare` flipped to true on the primary and the
base branch checked out into the linked worktree. Instead, merge from the
primary checkout, or omit `--delete-branch` and clean up explicitly:
`git push origin --delete <branch>`, then `git worktree remove <path>` and
`git branch -d <branch>`.

## Where Everything Else Lives

- Build and test presets, CI tiers, benchmark protocols: `CONTRIBUTING.md`.
- Hook and CI enforcement mechanics: `docs/git-hooks.md`.
- C++ style and header policy: `docs/style.md`.
- Test conventions and the per-test catalogue: `tests/AGENTS.md`.
- Documentation tree map: `docs/README.md`.
