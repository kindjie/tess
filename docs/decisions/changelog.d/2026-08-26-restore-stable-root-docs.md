## 2026-08-26 - Restore stable documentation at root URLs

The canonical documentation returns to stable root paths such as `/guide/`
and `/performance/`. This supersedes the 2026-08-21 decision to make
`/latest/` canonical. Search engines retained and served the established root
URLs after that migration, while those URLs returned 404; the version prefix
also produced a malformed social-image URL and moved the only sitemap away
from its established location.

The publication keeps mike for `/dev/` and immutable `/<major>.<minor>/`
archives. A small deterministic assembly step copies the complete verified
stable build to the root. `/latest/` HTML becomes an immediate exact redirect
to the corresponding root page, while its non-HTML files remain available for
old demo and asset URLs. Version trees receive `noindex, follow` only in the
served Pages artifact; the storage copies remain unchanged. The root sitemap
contains canonical root URLs only.

The assembler owns only paths recorded in a storage-branch manifest and fails
closed on an unowned collision. The first deployment can bootstrap from the
existing `latest` copy; subsequent development deployments preserve the root,
and stable releases replace it from the same complete artifact already checked
for links and browser behavior. Main, tag, and publishing-dispatch workflows
share one publication queue so storage and Pages artifact updates cannot race.

A GCP edge remains deferred. Direct GitHub Pages is the smaller, portable
solution for canonical URLs and ordinary static documentation. An edge layer
becomes justified only if a separately designed threaded WebAssembly feature
needs cross-origin isolation headers or another requirement GitHub Pages
cannot express.
