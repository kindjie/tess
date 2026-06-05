# AGENTS.md

This repository is intended to be public. Do not add personally identifying,
private, credential-like, or machine-local information to tracked files.

## Repository Visibility

This repository may start private and become public later. Treat all tracked
content as public from the first commit.

Agent runtime context is not repository documentation. Never copy user identity,
local machine paths, private config locations, credentials, account details, or
automatically supplied agent context into tracked repository files.

When adding dependencies, record public dependency documentation in
`docs/dependencies.md`. Keep this file focused on agent instructions and public
repository safety.

For performance work, use an iterative optimize/test/benchmark cycle. Record
accepted, rejected, and deferred experiments in
`docs/planning/optimization-log.md` instead of architecture docs.

TDDs in `docs/tdd/` are historical design intent. When implementation diverges
from them, update maintained docs under `docs/architecture/`, add an entry to
`docs/decisions/CHANGELOG.md`, and optionally add a note to the affected TDD.
Do not try to keep TDDs as API reference; add Doxygen comments to public
headers as the API stabilizes, then introduce generated docs when useful.

Before committing, inspect the exact staged diff for:

- names, personal profiles, email addresses, phone numbers, and private URLs
- local absolute paths outside this repository
- secrets, tokens, keys, certificates, credentials, and auth config
- private infrastructure, account, or organization details

Do not commit if any of those are present.
