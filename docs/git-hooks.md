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
- `pre-push`: dev configure and build, then the affected-test subset
  selected by CTest labels from the pushed range's changed paths
  (subsystem headers map to `subsystem:` labels, test sources to
  `target:` labels; `prepush:always` tests run on every selective
  push). Anything unrecognized — tools, CMake files, core/storage or
  umbrella headers, test helpers — fails open to the full suite;
  docs-only, examples-only, and bench-only pushes configure and build
  without tests. A new remote branch uses the merge base with the
  locally tracked remote default branch when Git's destination name and
  push URL can be verified; this gives its first push the same path
  classification as later pushes. `TESS_PREPUSH_FULL=1` overrides every
  classification and runs the full suite plus the install and FetchContent
  smokes
  (never benchmarks or the python tool tests — CI's PR tier owns
  both: the bench-smoke job and the hook-backstop pytest). The label
  mapping is itself tested (`tests/test_git_hooks.py`), and the CI
  dev job asserts label propagation after every build.

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
(`<local-ref> <local-sha> <remote-ref> <remote-sha>`). The hook unions
the changed paths across every non-delete ref and classifies them for
test selection. For a new branch, it verifies Git's two destination
arguments against a configured remote and its effective push URLs,
then compares the pushed tip with the single merge base of that
remote's locally tracked symbolic default branch. The hook does not
fetch or know a pull request's base. Ordinary behind-staleness may
select extra work; rewritten or changed defaults and non-linear
history may narrow the local evidence. CI remains authoritative, and
`TESS_PREPUSH_FULL=1` is the local escape hatch. New tags and other new
refs, missing or cross-remote defaults,
ambiguous or disconnected merge bases, unresolvable ranges, malformed
or absent ref input, and any unrecognized path fail open to the full
suite. Delete-only pushes skip the checks entirely. When the pushed
commit is not the worktree HEAD the hook warns that it validates the
worktree, not that commit.

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
- tests/agents.d drift: `tests/agents.d/` must mirror the test set
  exactly. Every `add_executable(tess_...)` target in
  `tests/CMakeLists.txt` and every `tests/test_*.py` file needs a
  fragment named `<name>.md` whose first line is `# <name>` and whose
  body is nonempty, and a fragment matching neither is rejected as an
  orphan, so a renamed or removed test cannot leave stale documentation.

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
paths escape terminal control characters. The hook resolves the repository's
common Git directory, so one untracked pattern file protects the main checkout
and every linked worktree rather than silently creating per-worktree policy.
`tools/git_hooks.py install` seeds one escaped full-name expression from the
repository's local `user.name`; add other private project or infrastructure
names manually.

Pull-request CI intentionally runs only the tracked generic patterns. Do not
pass private names or expressions into a workflow that executes code from an
untrusted pull-request head: command output, failures, or modified workflow
code could disclose them. The common-Git-dir local hook is the private-policy
boundary; CI remains the public generic backstop.

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
