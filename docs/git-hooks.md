# Git Hooks

This repository uses native Git hooks with a Python runner to keep local commits
clean before CI sees them.

## Requirements

- Git 2.54 or newer for config-defined hooks.
- Python 3.10 or newer.
- `clang-format` and `git-clang-format` for C++ formatting checks.
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
  benchmark files, CMake files, or public headers changed since upstream.

Hooks do not rewrite files or staged content. Fix the reported issue, stage the
result, and retry the Git command.

The token limit check runs through `uv run --frozen --group dev` so it uses the
checked-in development dependency lock instead of resolving packages during the
commit.

## Bypass

Use `--no-verify` only for emergencies. Run the skipped checks manually before
pushing follow-up work.
