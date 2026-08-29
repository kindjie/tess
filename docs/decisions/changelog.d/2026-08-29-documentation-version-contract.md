## 2026-08-29 - Publish exact candidates and name the development tree `main`

This supersedes the 2026-08-21 policy that published development documentation
at `/dev/` and skipped prerelease tags, while preserving the 2026-08-26 decision
that the newest stable documentation remains canonical at root URLs.

Main pushes publish a mutable, non-indexed `/main/` tree titled `main
(unreleased)`. `/dev/` remains as a path-preserving HTML redirect tree with its
legacy non-HTML assets retained. Stable tags continue to replace their
`/<major>.<minor>/` release line and move the root only when they are the newest
stable version. RC tags publish immutable exact SemVer trees without moving the
root or `/latest/`.

The selector orders the newest stable release first, then unsuperseded RCs,
`main`, and older stable lines. Superseded RCs are hidden rather than deleted,
so immutable URLs survive both a newer candidate and GA. Manual republishing is
allowed only from the trusted `main` workflow and checks out the exact requested
tag as documentation source; current workflow tooling retains publication
authority.

GitHub Pages remains a static host. `/latest/` and `/dev/` therefore use tested
meta-refresh and JavaScript redirects rather than claiming unavailable HTTP 301
semantics. An edge service remains deferred until a separate requirement
justifies its operational cost.
