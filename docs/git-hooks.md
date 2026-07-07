# Git Hooks

This repository uses native Git hooks with a Python runner to keep local
commits clean before CI sees them. CI runs the same checks over the full
repository, so CI is the authoritative backstop; the hooks remain the fast
local path.

## Requirements

- Git 2.54 or newer for config-defined hooks.
- Python 3.10 or newer.
- `clang-format` and `git-clang-format` for C++ formatting checks. The dev
  dependency group pins `clang-format` (and its bundled `git-clang-format`)
  on PyPI; after `uv sync --group dev` the hooks prefer the pinned binaries
  in `.venv/` and otherwise fall back to whatever is on `PATH`, so the hooks
  stay usable without `uv`.
- `uv` for the locked staged-file `tiktoken` size check.
- CMake for pre-push build and test checks.

Older Git versions are supported through `core.hooksPath`.

## Install

On macOS and Linux:

```sh
python3 tools/git_hooks.py install
```

On Windows:

```powershell
py -3 tools/git_hooks.py install
```

The installer prefers Git's config hook interface when `git hook list` is
available. Otherwise it sets `core.hooksPath` to `tools/git-hooks`.

## Hooks

- `pre-commit`: staged whitespace, conflict markers, public-safety patterns,
  C++ formatting, staged-file token limits, and GitHub noreply email.
- `commit-msg`: non-empty subject and 72-character subject limit.
- `pre-push`: dev configure, build, and tests. Benchmark build runs when
  benchmark files, CMake files, or public headers changed in the pushed
  range.

Hooks do not rewrite files or staged content. Fix the reported issue, stage
the result, and retry the Git command.

The token limit check runs through `uv run --frozen --group dev` so it uses
the checked-in development dependency lock instead of resolving packages
during the commit.

### Pre-push range semantics

Git feeds the pre-push hook one line per ref being pushed
(`<local ref> <local sha> <remote ref> <remote sha>`). The hook parses those
lines and:

- skips the build entirely when the push only deletes remote refs;
- decides whether to build the benchmark preset from the pushed range
  (`<remote sha>..<local sha>`) instead of guessing from the upstream. A
  new remote ref, an unresolvable range, or a manual run without an
  upstream all fall back to building the benchmarks (the honest default);
- warns loudly when a pushed sha is not the current worktree `HEAD`.

Known limitation: the build and tests always run against the current
worktree, not a checkout of the pushed commit. When the pushed sha differs
from `HEAD` (for example `git push origin other-branch`), the hook still
validates the worktree and prints a warning naming the pushed ref. CI
validates the pushed commits exactly, so treat the warning as a reminder
that local results may not match CI.

## CI backstop

Local hooks are opt-in, and `--no-verify` or a fresh clone bypasses them.
The `ci` subcommand runs the same shared checks over all tracked files in
the worktree, and the `Hook Backstop Checks` CI job runs it on every push
and pull request:

```sh
uv run --frozen --group dev python tools/git_hooks.py ci
```

It runs, using the same pattern lists and limits as the pre-commit hook:

- conflict markers over tracked text files;
- public-safety deny patterns over tracked text files;
- a repo-wide `clang-format --dry-run -Werror` over tracked C++ files using
  the pinned `clang-format` from the dev dependency group;
- the 24000-token `tiktoken` limit over tracked text files;
- tests/AGENTS.md drift: every `add_executable(tess_...)` test target in
  `tests/CMakeLists.txt` must be documented in `tests/AGENTS.md`.

The hook-only checks (`user.email` and commit-message subject) do not run
in `ci` mode. The hook runner's unit tests live in
`tests/test_git_hooks.py` and run in the same CI job:

```sh
uv run --frozen --group dev pytest tests/test_git_hooks.py
```

## Bypass

Use `--no-verify` only for emergencies. Run the skipped checks manually
before pushing follow-up work; CI will still enforce the tracked-file
checks above.
