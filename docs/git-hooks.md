# Git Hooks

This repository uses native Git hooks with a Python runner to keep local
commits clean before CI sees them. CI runs the same checks over the full
repository, so CI is the authoritative backstop; the hooks remain the fast
local path.

## Requirements

- Git 2.54 or newer for config-defined hooks.
- Python 3.10 or newer.
- `clang-format` and `git-clang-format` for C++ formatting checks. The dev
  requirements pin `clang-format` (and its bundled `git-clang-format`) on
  PyPI. The hooks prefer pinned binaries installed in `.venv/` and otherwise
  fall back to `PATH`, so they stay usable with a system installation.
- `uv` for the locked staged-file `tiktoken` size check.
- CMake for pre-push build and test checks.

Older Git versions are supported through `core.hooksPath`.

Run repository tools in the isolated, version- and hash-locked environment:

```sh
uv run --no-project --with-requirements requirements-dev.txt -- \
  <command>
```

The requirements lock fixes package versions, markers, and every permitted
distribution hash. Regenerate it only with the exact command in its header.

To make the pinned formatters directly available to Git hooks, create a local
environment once (and repeat after the requirements change):

```sh
uv venv
uv pip sync --require-hashes requirements-dev.txt
```

## Install

On macOS and Linux:

```sh
python3 tools/git_hooks.py install
```

On Windows:

```powershell
py -3 tools/git_hooks.py install
```

The installer prefers Git's config hook interface only when its
`git hook list` feature probe succeeds. Otherwise it sets `core.hooksPath` to
`tools/git-hooks`.

## Hooks

- `pre-commit`: staged whitespace, conflict markers, public-safety patterns,
  C++ formatting, staged-file token limits, and GitHub noreply email.
- `commit-msg`: non-empty subject and 72-character subject limit.
- `pre-push`: dev configure, build, and tests. Benchmark build runs when
  benchmark files, CMake files, or public headers changed in the pushed
  range.

Hooks do not rewrite files or staged content. Conflict, public-safety, and
token checks read exact blobs from the Git index, including a symlink's link
text rather than its worktree target. Unmerged entries, gitlinks, missing
objects, and malformed Git output fail closed. Fix the reported issue, stage
the result, and retry the Git command.

The token limit check runs through `uv run --no-project` with the checked-in
compiled requirements lock. `requirements-dev.in` contains the three direct
tool pins; `requirements-dev.txt` pins every transitive dependency and marker.
Regenerate it with uv 0.11.28 by running `tools/compile_requirements.sh`; the
wrapper checks the uv version, fixes the package-index cutoff, and records
itself as the stable lock header.

`uv` normally uses a cache under the user's home directory. In a restricted
environment where that location is not writable, point `UV_CACHE_DIR` at a
writable ignored directory for hook and repository-tool commands:

```sh
UV_CACHE_DIR=build/uv-cache \
  uv run --no-project --with-requirements requirements-dev.txt -- \
  python tools/git_hooks.py ci
```

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
The `ci` subcommand runs the same shared checks over all tracked paths; its
content scanners use the index snapshot. The `Hook Backstop Checks` CI job
runs it on every push and pull request. CI creates `.venv` with
`uv pip sync --require-hashes requirements-dev.txt`, then invokes the pinned
tools directly.

```sh
uv run --no-project --with-requirements requirements-dev.txt -- \
  python tools/git_hooks.py ci
```

It runs, using the same pattern lists and limits as the pre-commit hook:

- conflict markers over exact indexed text blobs;
- public-safety deny patterns over indexed filenames and text blobs;
- a repo-wide `clang-format --dry-run -Werror` over tracked C++ files using
  the pinned `clang-format` from the development requirements;
- the strict under-24000-token limit over indexed text blobs, using GPT-5's
  `o200k_base` tokenizer;
- tests/AGENTS.md drift: every `add_executable(tess_...)` test target in
  `tests/CMakeLists.txt` must be documented in `tests/AGENTS.md`.

The repository-wide formatter reads the checked-out C++ files after obtaining
their path list from the index. CI uses a clean checkout; before a local `ci`
run, either stage or set aside unrelated C++ worktree edits.

Identity-specific deny patterns must not be tracked in this public-intended
repository. Add them as byte-oriented regular expressions, one per line, to
`.git/tess-private-patterns`. Blank lines and lines beginning with `#` are
ignored, and matching is case-insensitive. The hook combines that local file
with its tracked generic secret, profile, and machine-path patterns. It scans
the raw bytes of every indexed filename, including binary-file names, then the
contents of every non-binary indexed blob regardless of filename or extension.
The same NUL-byte heuristic used by Git keeps binary payloads out of content
checks. Git path queries and push-range diffs are NUL-delimited, and diagnostic
paths escape terminal control characters. `git rev-parse --git-path` locates
the local pattern file correctly in both ordinary and linked worktrees.
`tools/git_hooks.py install` seeds one escaped full-name expression from the
repository's local `user.name`; add other private project or infrastructure
names manually.

The token gate replaces malformed UTF-8 bytes with the Unicode replacement
character before tokenization so malformed input cannot disappear from the
count. A file at exactly 24000 tokens fails because repository files must stay
under the limit.

The hook-only checks (`user.email` and commit-message subject) do not run in
`ci` mode. Both current numeric-prefix and legacy GitHub noreply addresses are
accepted. The hook runner's unit tests live in
`tests/test_git_hooks.py` and run in the same CI job:

```sh
uv run --no-project --with-requirements requirements-dev.txt -- \
  pytest tests/test_git_hooks.py
```

## Bypass

Use `--no-verify` only for emergencies. Run the skipped checks manually
before pushing follow-up work; CI will still enforce the tracked-file
checks above.
