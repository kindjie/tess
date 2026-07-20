# Documentation hosting runbook

The public documentation is a static MkDocs site deployed by GitHub Actions to
GitHub Pages. The source repository can remain private on GitHub Pro, although
the published site is public. Hosting needs no separate server, database, or
deploy credential.

## GitHub settings

1. Keep the repository private through review, merge, and release-candidate
   validation. Inspect only the commits newly entering `main`.
2. In **Settings > Pages**, select **GitHub Actions** as the source. Publish the
   public site from the private repository for final pre-launch validation.
3. Set the custom domain to `tess.owx.dev` and enable HTTPS after GitHub has
   issued the certificate.
4. In the owning GitHub account's Pages settings, verify `owx.dev`. GitHub will
   provide the exact `_github-pages-challenge-kindjie` TXT value; it is an
   account-specific value and must not be guessed or copied from another
   account.

The workflow uses only the scoped `GITHUB_TOKEN`, with `contents: read` for the
build and `pages: write` plus `id-token: write` for deployment.

## DNS

The domain is currently delegated to Cloudflare. Add this proxied-off,
DNS-only record:

| Type | Name | Target | TTL |
| --- | --- | --- | --- |
| CNAME | `tess` | `kindjie.github.io` | Auto |

Keep the record DNS-only. The current wildcard resolves `tess.owx.dev` to
`34.111.69.97`; inventory hostnames that intentionally depend on the wildcard,
replace them with explicit records, then remove or narrow it before enabling
the custom domain. Add and retain GitHub's generated
`_github-pages-challenge-kindjie` TXT record before adding the CNAME.

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
