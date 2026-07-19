# Documentation hosting runbook

The public documentation is a static MkDocs site deployed by GitHub Actions to
GitHub Pages. Hosting is free while the repository is public and needs no
separate server, database, or deploy credential.

## GitHub settings

1. Make the repository public after the public-safety audit passes.
2. In **Settings > Pages**, select **GitHub Actions** as the source.
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

Keep the record DNS-only until GitHub has validated the domain and issued the
TLS certificate. There is currently no conflicting `tess.owx.dev` record and
no restrictive CAA record. The repository includes `docs/CNAME` so the built
Pages artifact also carries the intended hostname.

## Local preview

```sh
uv venv --python 3.12 .venv-docs
uv pip sync --python .venv-docs/bin/python \
  --require-hashes requirements-docs.txt
.venv-docs/bin/mkdocs serve
```

CI runs `mkdocs build --strict`; broken internal links and configuration
warnings therefore block deployment.
