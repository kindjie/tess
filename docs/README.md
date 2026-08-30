# tess Documentation

This tree separates current project documentation from historical design
intent.

## Current Docs

Published on the documentation site:

- [Getting Started](getting-started.md): tutorial from shapes and schemas
  to the schedule loop and render bridge.
- [Tutorials](tutorials.md): connected learning paths for pathfinding,
  flow-style steering, colony composition, strategy selection, and congestion
  pricing.
- [Decision guide](guide/README.md): one page per architectural decision
  — branches, criteria, and links into the tutorial and concept notes.
- [Installation](packaging.md): supported installation paths and registry
  status.
- [Examples](examples.md): the annotated example catalog.
- [Use cases](use-cases.md): robotics, agent-based-modeling, and
  headless-server framings of the shipped examples.
- [Architecture](architecture/README.md): maintained design notes that should
  track implementation.
- [Reference](reference.md): landing page for architecture, terminology,
  compatibility evidence, and generated API documentation.
- [Terminology](terminology.md): canonical nouns, actions, states, time units,
  and qualifiers shared by the public API and maintained documentation.
- [Performance](performance.md): adopter-facing benchmark expectations and
  the trend snapshot.
- [Pathfinding strategy comparison](pathfinding-strategy-comparison.md):
  paired API mechanics, an embedded C++/WebAssembly call-shape demo, and
  benchmark workload references for A*, route caches, weighted batches, and
  distance fields.
- [Roadmap](roadmap.md): released vs landed-but-unreleased vs release-gated,
  deferred, and out-of-scope work.
- [Support](support.md): adopter help, issue, and compatibility guidance;
  the canonical pre-1.0 stability statement.
- [For agents](for-agents.md): machine-adoption recipe; `llms.txt` is
  the site-root page map for LLM tooling.

Contributor and operations pages (in-repo only; excluded from the site):

- [Dependencies](dependencies.md): external library choices and documentation.
- [Hosting](hosting.md): GitHub Pages and custom-domain runbook.
- [Style](style.md): C++ coding style and formatting policy.
- [Git Hooks](git-hooks.md): local commit and push guardrails.
- [Release process](releasing.md): exact-SHA release mode, RC observation, and
  GA checklist.
- [doxygen-awesome](doxygen-awesome/README.md): vendored Doxygen theme.
- [Fonts](assets/fonts/README.md): vendored heading font.
- `favicon.ico`: site-root fallback icon (rasterized from
  `assets/tess-symbol.svg` at 16/32/48 px) so `/api/` pages resolve a
  favicon; MkDocs pages keep the SVG favicon from `mkdocs.yml`.
- `robots.txt`: permissive crawler policy with the canonical sitemap URL.
- `assets/tess-social-preview.png`: 2560x1280 social-preview card (dark
  wordmark + tagline); uploaded manually in the repository settings for
  GitHub, and published with the site as the `og:image`/`twitter:image`
  card referenced from `overrides/main.html`.

## Internal Records

- [Planning](planning/README.md): maintained runbooks plus clearly separated
  implementation, completion, audit, and other point-in-time records.
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

When implementation diverges from a TDD, follow the divergence procedure in
the root `AGENTS.md`: update the maintained architecture docs, add a
design-decision fragment under `decisions/changelog.d/`, and optionally add a
short note at the top of the affected TDD pointing to the newer source of
truth.

## Generated documentation

The public authored site is built with MkDocs. A CMake-driven Doxygen target
generates the API reference published under `/api/`; it excludes
`tess::detail` and treats documentation errors as build failures. Its
version-relative navigation returns to the matching Docs, Learn, and Reference
pages in root, development, release-line, and exact prerelease trees. Authored
symbol links use canonical same-origin API URLs; publication localizes them
when the generated target exists in that version tree. Every C++ fence in
maintained Markdown is copied from a named region in a compiled example or
test;
`tools/check_doc_snippets.py` rejects drift and unbacked fences. Historical
design and planning records are exempt because they are non-authoritative.
`tools/check_doc_versions.py` keeps the development and latest-release
installation paths distinct.
