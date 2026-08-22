## 2026-08-21 - Publish versioned documentation trees

- Adopted mike for versioned documentation. The site publishes `/latest/`,
  a per-release `/<major>.<minor>/` archive, and `/dev/` from `main`, with
  Material's version selector reading mike's `versions.json`. Before this,
  one site was continuously deployed from `main` while describing itself as
  the current release, so development changes were presented as released
  documentation.
- Rejected keeping released documentation at flat root paths with only a
  `/dev/` prefix added. mike cannot serve real content from the site root —
  every option either nests content under a version or emits a redirect — so
  that layout would have required a bespoke two-tree build rather than the
  supported tool. The migration cost was one-time and internal: 51 documented
  links moved under `/latest/`, and no published release note contained one.
- Aliases are copies rather than mike's default symlinks. GitHub Pages does
  not serve symlinked directories, and `latest` is where every documented
  link resolves, so a symlink alias would return 404 for the entire released
  site.
- The generated API reference and WebAssembly demos are staged inside `docs/`
  before deploy. mike runs mkdocs itself and has no prebuilt-directory mode,
  so content that must appear inside a version tree has to be somewhere
  mkdocs copies; both remain generated and gitignored.
- `gh-pages` is storage rather than the served branch. The workflow commits
  each version tree there and then uploads the whole branch as the Pages
  artifact, so the Pages source stays GitHub Actions and the custom domain
  keeps working from repository settings. Serving the branch directly would
  have required a repository settings change and a tracked `CNAME`, and would
  have broken the site if the two were not switched together.
- Only the publish job carries a write token, and its checkout does not
  persist credentials; it authorizes its own remote commands instead. The
  build job that runs the container and browser steps stays read-only.
