# Vendored doxygen-awesome-css

Styling for the generated Doxygen API reference (`tess_docs` target).

- Upstream: <https://github.com/jothepro/doxygen-awesome-css>
- Vendored release: `v2.4.2`
  (tag commit `d52eafe3e9303399fda15661f3d7bb8fe3d7eabc`)
- License: MIT (see [`LICENSE`](LICENSE))

Vendored files and their sha256 checksums:

```text
5ec49e2dfd097f6b5384e3aae0476eab47748e311fc70e207925f8fcc37477b9  doxygen-awesome.css
dc7ddd235375b71ecb0af920faa6b925ee9445ac617f3bc962b0b0db97da7b4f  doxygen-awesome-sidebar-only.css
```

`tess-theme.css` is a tess-authored override that aligns the theme's
primary colors with the documentation site's deep-purple palette. Dark
mode follows `prefers-color-scheme` (the upstream JavaScript toggle is
deliberately not vendored: it requires a Doxygen-version-coupled custom
`HTML_HEADER`, and the API reference tracking the OS setting is
acceptable).

Re-vendor against a new upstream release with:

```sh
curl -fsSL -o /tmp/doxygen-awesome.tar.gz \
  https://github.com/jothepro/doxygen-awesome-css/archive/refs/tags/<TAG>.tar.gz
tar -x -f /tmp/doxygen-awesome.tar.gz -C /tmp
cp /tmp/doxygen-awesome-css-<VERSION>/{doxygen-awesome.css,doxygen-awesome-sidebar-only.css,LICENSE} \
  docs/doxygen-awesome/
shasum -a 256 docs/doxygen-awesome/*.css
```

Then update this file's release, commit, and checksum records, and rebuild
`tess_docs` to confirm rendering against the pinned Doxygen version in
`.github/workflows/pages.yml`.
