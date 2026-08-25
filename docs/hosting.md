# Documentation hosting runbook

The public documentation is a static MkDocs site with a Doxygen API reference
under `api/`, deployed by GitHub Actions to GitHub Pages. Hosting needs no
separate server, database, or deploy credential.

## Version trees

The site publishes one tree per documented version, managed by
[mike](https://github.com/jimporter/mike):

| URL | Content |
| --- | --- |
| `/` | Redirect to the default version |
| `/latest/` | Newest release; every documented deep link points here |
| `/<major>.<minor>/` | That release, kept as it was published |
| `/dev/` | `main`, republished on every push |

Each tree is self-contained: its own pages, its own `api/` reference, and its
own `demo/` builds. The version selector in the header comes from Material and
reads mike's `versions.json`.

Two choices in that pipeline are load-bearing and should not be changed
casually:

- **Aliases are copies, not symlinks** (`--alias-type copy`). GitHub Pages
  does not serve symlinked directories, and `latest` is where every published
  link resolves, so a symlink alias would return 404 for the whole site.
- **The generated API and demo trees are staged into `docs/` before deploy.**
  mike runs mkdocs itself and has no prebuilt-directory mode, so anything that
  must appear inside a version tree has to be somewhere mkdocs copies. Both
  are generated and gitignored, matching the fetched Mermaid runtime.

`gh-pages` is storage, not the served branch. The workflow commits each
version tree there, then checks the whole branch out and uploads it as the
Pages artifact, so the Pages source stays **GitHub Actions** and the custom
domain keeps working from repository settings. Only the publish job holds a
write token, and it never touches `main`.

### Publishing a version

- Pushing to `main` publishes `/dev/`.
- Pushing a stable `v<major>.<minor>.<patch>` tag publishes
  `/<major>.<minor>/`, moves the `latest` alias, and sets the site
  default.
- A prerelease tag (for example `v1.0.0-rc.1`) publishes nothing:
  `/dev/` already tracks the candidate during its observation window,
  and `latest` keeps pointing at the newest stable release.
- Dispatching the workflow against a ref with `publish_version` set does the
  same for that ref, which is how an existing tag is published retroactively.

Pull requests build and verify without publishing anything.

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
deployment.
