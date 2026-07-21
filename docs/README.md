# tess Documentation

This tree separates current project documentation from historical design
intent.

## Current Docs

Published on the documentation site:

- [Getting Started](getting-started.md): tutorial from shapes and schemas
  to the schedule loop and render bridge.
- [Installation](packaging.md): supported installation paths and registry
  status.
- [Examples](examples.md): the annotated example catalog.
- [Architecture](architecture/README.md): maintained design notes that should
  track implementation.
- [Performance](performance.md): adopter-facing benchmark expectations and
  the trend snapshot.
- [Support](support.md): adopter help, issue, and compatibility guidance;
  the canonical pre-1.0 stability statement.

Contributor and operations pages (in-repo only; excluded from the site):

- [Dependencies](dependencies.md): external library choices and documentation.
- [Hosting](hosting.md): GitHub Pages and custom-domain runbook.
- [Style](style.md): C++ coding style and formatting policy.
- [Git Hooks](git-hooks.md): local commit and push guardrails.
- [doxygen-awesome](doxygen-awesome/README.md): vendored Doxygen theme.
- [Fonts](assets/fonts/README.md): vendored heading font.
- `favicon.ico`: site-root fallback icon (rasterized from
  `assets/tess-symbol.svg` at 16/32/48 px) so `/api/` pages resolve a
  favicon; MkDocs pages keep the SVG favicon from `mkdocs.yml`.
- `assets/tess-social-preview.png`: 2560x1280 GitHub social-preview card
  (dark wordmark + tagline); uploaded manually in the repository
  settings, excluded from the published site.

## Internal Records

- [Planning](planning/README.md): milestone plans and audit records;
  internal working documents, not a guide to current behavior.
- [Optimization Log](planning/optimization-log.md): accepted, rejected, and
  deferred performance experiments.
- [Benchmark Calibration](planning/benchmark-calibration.md): threshold
  calibration methodology and history.

## Design History

- [Repository History](history.md): identifies the canonical repository
  history and explains how to interpret retained pre-public pull requests.
- [TDD Archive](tdd/README.md): original technical design documents plus
  proposed addenda that continue to land there. Historical: these explain
  design intent and rationale, but they are non-authoritative and not a
  guide to current behavior - maintained architecture docs and code are
  the source of truth for the current implementation.
- [Design Changelog](decisions/CHANGELOG.md): records meaningful changes from
  the original TDDs.

When implementation diverges from a TDD, update the maintained architecture
docs, add a design changelog entry, and optionally add a short note at the top
of the affected TDD pointing to the newer source of truth.

## Generated documentation

The public authored site is built with MkDocs. A CMake-driven Doxygen target
generates the API reference published under `/api/`; it excludes
`tess::detail` and treats documentation errors as build failures. Every C++
fence in maintained Markdown is copied from a named region in a compiled
example or test;
`tools/check_doc_snippets.py` rejects drift and unbacked fences. Historical
design and planning records are exempt because they are non-authoritative.
`tools/check_doc_versions.py` keeps the development and latest-release
installation paths distinct.
