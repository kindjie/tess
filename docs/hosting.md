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
| `/latest/` | Exact redirects; compatibility assets retained |
| `/<major>.<minor>/` | Latest patch in that release line; noindexed archive |
| `/<major>.<minor>.<patch>-rc.<n>/` | Immutable, noindexed RC archive |
| `/main/` | Non-indexed development snapshot, titled `main (unreleased)` |
| `/dev/` | Redirects to `/main/`; legacy assets retained |

Each tree is self-contained: its own pages, its own `api/` reference, and its
own `demo/` builds. The version selector in the header comes from Material and
reads mike's `versions.json`.

Two choices in that pipeline are load-bearing and should not be changed
casually:

- **Aliases are copies, not symlinks** (`--alias-type copy`). GitHub Pages
  does not serve symlinked directories. After mike updates `latest`, the
  publication helper replaces its HTML with exact root redirects but retains
  copied Wasm, JavaScript, CSS, images, and other non-HTML resources so old
  embeds do not break. Four per-tree resources are the exception: artifact
  preparation removes each tree's `sitemap.xml`, `sitemap.xml.gz`,
  `llms.txt`, and nested `robots.txt` from what is served (they remain in
  storage), because each is an independently indexable resource inside a
  tree whose HTML policy is retirement.
- **The generated API and demo trees are staged into `docs/` before deploy.**
  mike runs mkdocs itself and has no prebuilt-directory mode, so anything that
  must appear inside a version tree has to be somewhere mkdocs copies. Both
  are generated and gitignored, matching the fetched Mermaid runtime.

`gh-pages` is storage, not the served branch. The workflow commits each
version tree there, assembles the stable root and compatibility redirects,
then checks the whole branch out and uploads it as the Pages artifact. The
assembler owns only paths in its manifest and fails closed before deleting
version trees or unknown operator-owned files. The first deployment can
bootstrap the root from the existing `latest` copy; later `main` and RC
deployments leave the stable root unchanged.

The root `robots.txt` is the exception: the assembler writes it from its
own constant on every run, including runs that otherwise leave the established
root alone. Those ordinary runs also repair legacy root identity metadata that
names either `/latest` or `/latest/`; the final artifact check rejects stale
root canonical, Open Graph, Twitter, and JSON-LD URLs. The root policy is
authored rather than inherited because the
stable copy comes from a released version tree, so directives that were
correct at that release would stay pinned at the root until the next one
— which is how `Disallow: /dev/` outlived the switch to `noindex,
follow` and kept crawlers from reading the very directive meant to
retire those pages. A root `robots.txt` the manifest does not own is
never rewritten; publication fails instead, so an operator edit is
resolved deliberately.

Before upload, the workflow prepares the ephemeral Pages artifact: it adds
`noindex, follow` to `/main/`, `/dev/`, stable archives, and exact RC HTML,
removes each version
tree's `sitemap.xml`, `sitemap.xml.gz`, `llms.txt`, and nested `robots.txt`
(those URLs return 404 from the served site), localizes a tree's same-origin
anchors onto its own pages where the target exists in-tree, and repoints a
frozen version tree's stale `/latest/`-prefixed head metadata at the tree's
own URL. None of that rewrites the stored archive -- the storage branch is
committed before preparation runs, which the workflow-order tests pin.
`/latest/` HTML has an immediate redirect, canonical root URL, and the same
`noindex` policy. The root sitemap contains only canonical root URLs, and
`robots.txt` names it so crawlers can read page directives.

### Publishing a version

- Pushing to `main` publishes `/main/`. The publisher refreshes `/dev/` as a
  path-preserving HTML redirect tree and retains its non-HTML compatibility
  assets.
- Pushing a stable `v<major>.<minor>.<patch>` tag publishes
  `/<major>.<minor>/`; the stable root and `latest` compatibility tree move
  only when the tag's version is at least the currently aliased one, so a
  patch on an older minor line refreshes its own tree without pointing
  the site backward.
- Pushing an RC tag such as `v1.0.0-rc.1` publishes its exact, immutable
  `/1.0.0-rc.1/` tree without moving the stable root or `/latest/`.
- To republish an existing tag, dispatch the workflow from `main` with
  `publish_tag` set to the exact stable or RC tag. The build and Mike source
  come from that tag, including source-content checks and demo tests that are
  coupled to it. The current trusted workflow commit supplies artifact
  assembly, generated-site link validation, Doxygen stamping, selection, and
  deployment. A later authored-content check is skipped only when it does not
  exist in the selected historical source. A source whose CMake version does
  not match the requested tag fails before publication.
- The selector inventory is normalized after every Mike deployment. The
  newest stable release comes first, followed by the highest unsuperseded RCs,
  `main`, and older stable lines. A newer RC hides earlier candidates for the
  same base version; GA hides all candidates for that base. Hidden RC entries
  and their exact directories remain available.

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
`/latest/` and `/dev/` redirects, selector ordering, root identity metadata,
and non-indexing metadata on every version tree. Doxygen's project number and
generated canonical paths use the exact selected label and publication path.
