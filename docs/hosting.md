# Documentation hosting runbook

The public documentation is a static MkDocs site with a Doxygen API reference
under `api/`, deployed by GitHub Actions to GitHub Pages. Hosting needs no
separate server, database, or deploy credential.

## Version trees

The site publishes one tree per documented version, managed by
[mike](https://github.com/jimporter/mike):

| URL | Content |
| --- | --- |
| `/` | Newest stable release and the canonical public URL |
| `/latest/` | Compatibility redirects plus retained non-HTML assets |
| `/<major>.<minor>/` | Immutable, non-indexed release archive |
| `/dev/` | Non-indexed development snapshot from `main` |

Each tree is self-contained: its own pages, its own `api/` reference, and its
own `demo/` builds. The version selector in the header comes from Material and
reads mike's `versions.json`.

Two choices in that pipeline are load-bearing and should not be changed
casually:

- **Aliases are copies, not symlinks** (`--alias-type copy`). GitHub Pages
  does not serve symlinked directories. After mike updates `latest`, the
  publication helper replaces its HTML with exact root redirects but retains
  copied Wasm, JavaScript, CSS, images, and other non-HTML resources so old
  embeds do not break.
- **The generated API and demo trees are staged into `docs/` before deploy.**
  mike runs mkdocs itself and has no prebuilt-directory mode, so anything that
  must appear inside a version tree has to be somewhere mkdocs copies. Both
  are generated and gitignored, matching the fetched Mermaid runtime.

`gh-pages` is storage, not the served branch. The workflow commits each
version tree there, assembles the stable root and compatibility redirects,
then checks the whole branch out and uploads it as the Pages artifact. The
assembler owns only paths in its manifest and fails closed before deleting
version trees or unknown operator-owned files. The first deployment can
bootstrap the root from the existing `latest` copy; later `main` deployments
leave the stable root unchanged.
The root `robots.txt` travels with that stable copy; development and
older-minor publications cannot replace it from their own source refs.

Before upload, the workflow adds `noindex, follow` to `/dev/` and numeric
archive HTML in the ephemeral Pages artifact. That metadata does not rewrite
the stored archive. `/latest/` HTML has an immediate redirect, canonical root
URL, and the same `noindex` policy. The root sitemap contains only canonical
root URLs, and `robots.txt` names it so crawlers can read page directives.

### Publishing a version

- Pushing to `main` publishes `/dev/`.
- Pushing a stable `v<major>.<minor>.<patch>` tag publishes
  `/<major>.<minor>/`; the stable root and `latest` compatibility tree move
  only when the tag's version is at least the currently aliased one, so a
  patch on an older minor line refreshes its own tree without pointing
  the site backward.
- A prerelease tag (for example `v1.0.0-rc.1`) publishes nothing:
  `/dev/` already tracks the candidate during its observation window,
  and `latest` keeps pointing at the newest stable release.
- Accepted residual: the alias guard compares versions numerically, so
  a mistyped `publish_version` that is numerically newer than every
  release (say `9.9`) would create a junk tree and take `latest` plus the
  stable root; no guard can distinguish it from a legitimate retroactive
  publish of a genuinely newer tag, so the dispatch input is the operator's
  responsibility.
- Dispatching the workflow against a ref with `publish_version` set
  (validated as `<major>.<minor>`) does the same for that ref, which is
  how an existing tag is published retroactively. A dispatch that moves the
  stable root uses the complete verified build from the selected ref.

Pull requests build and verify without publishing anything.

Every publishing run receives its own concurrency identity. Before touching
the storage branch, its publish job waits for every active non-PR attempt that
started earlier. Attempt start time matters because rerunning an old workflow
must queue behind a newer publication already in flight. Polling asks GitHub
only for active statuses, so its API cost does not grow with workflow history.
This FIFO turn preserves clustered main, tag, publishing-dispatch, and rerun
attempts; unlike GitHub's native concurrency queue, it does not discard an
existing pending run when a newer one arrives. The turn also covers Pages
deployment, so storage and the served artifact move in the same order.
Pull-request verification keeps a separate per-PR group and cancels only
superseded runs of that PR.

## GitHub settings

1. In **Settings > Pages**, select **GitHub Actions** as the source.
2. Set the custom domain to `tess.owx.dev` and enable HTTPS after GitHub has
   issued the certificate.
3. In the owning GitHub account's Pages settings, verify the apex domain.
   GitHub provides an account-specific `_github-pages-challenge-<account>`
   TXT value; it must not be guessed or copied from another account.

The workflow uses only the scoped `GITHUB_TOKEN`. The build gets
`contents: read` and `pages: read`; deployment alone gets `pages: write` plus
`id-token: write`.

## DNS

Add this record at the DNS provider, unproxied (DNS-only):

| Type | Name | Target | TTL |
| --- | --- | --- | --- |
| CNAME | `tess` | `kindjie.github.io` | Auto |

Keep the record DNS-only. Remove or narrow any wildcard record that would
otherwise answer for the hostname before enabling the custom domain, and add
GitHub's generated domain-verification TXT record before adding the CNAME.

The custom domain is configured in GitHub's Pages settings. A `CNAME` file in
the documentation source does not configure a custom Actions deployment and
is intentionally not tracked.

## Local preview

```sh
python3.12 -m venv .venv-docs
.venv-docs/bin/python -m pip install \
  --require-hashes --requirement requirements-docs.txt
python3 tools/fetch_mermaid.py
.venv-docs/bin/mkdocs serve
```

`tools/fetch_mermaid.py` places the pinned, SHA-256-verified Mermaid runtime
in `docs/assets/javascripts/` (gitignored — it exceeds the tracked-file token
budget) so diagram pages serve Mermaid from the site's own origin instead of
Material's unpkg.com fallback. A small inline proxy in `overrides/main.html`
preserves Material's `initialize()`/`render()` contract and loads the runtime
only when a page contains a diagram, including after instant navigation.
Without the fetched asset, a diagram render reports a runtime-load failure.

CI runs `mkdocs build --strict`, builds the `tess_docs` target with the pinned
Doxygen release, checks authored-site links, validates every Mermaid fence
against the self-hosted runtime (`tools/check_mermaid.py` — parse failures
are invisible to `--strict` and would otherwise ship as raw diagram source),
and loads the WebAssembly demos in headless Chrome. The pathfinding strategy
smoke also checks C++-derived cache, batch, and reachable-field values after
real-time readiness, not just successful module loading. The workflow then
copies the generated API
HTML into `build/site/api`. Doxygen warnings, broken authored-site links, an
invalid diagram, or a demo that does not reach its ready state block
deployment. `tools/publish_docs_root.py check` then validates the final tree
that is actually uploaded: stable root canonical and sitemap URLs,
`/latest/` redirects, and non-indexing metadata on version trees.
