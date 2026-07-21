# Documentation hosting runbook

The public documentation is a static MkDocs site deployed by GitHub Actions to
GitHub Pages. Hosting needs no separate server, database, or deploy
credential.

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
.venv-docs/bin/mkdocs serve
```

CI runs `mkdocs build --strict`, checks every generated local link and anchor,
and loads the WebAssembly demo in headless Chrome. Broken documentation or a
demo that does not reach its ready state blocks deployment.
