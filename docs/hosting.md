# Documentation hosting runbook

The public documentation is a static MkDocs site with a Doxygen API reference
under `/api/`, deployed by GitHub Actions to GitHub Pages. Hosting needs no
separate server, database, or deploy credential.

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
Material's unpkg.com fallback. Without it, local previews fall back to the
CDN fetch.

CI runs `mkdocs build --strict`, builds the `tess_docs` target with the pinned
Doxygen release, checks authored-site links, validates every Mermaid fence
against the self-hosted runtime (`tools/check_mermaid.py` — parse failures
are invisible to `--strict` and would otherwise ship as raw diagram source),
and loads the WebAssembly demo in headless Chrome. It then copies the
generated API HTML into `build/site/api`. Doxygen warnings, broken
authored-site links, an invalid diagram, or a demo that does not reach its
ready state block deployment.
