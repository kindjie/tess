# tess Documentation

This tree separates current project documentation from historical design
intent.

## Current Docs

- [Architecture](architecture/README.md): maintained design notes that should
  track implementation.
- [Planning](planning/README.md): milestone and benchmark plans.
- [Dependencies](dependencies.md): external library choices and documentation.

## Design History

- [TDD Archive](tdd/README.md): original technical design documents. These
  explain design intent and rationale, but they are not the authoritative
  source for current implementation behavior once code diverges.
- [Design Changelog](decisions/CHANGELOG.md): records meaningful changes from
  the original TDDs.

When implementation diverges from a TDD, update the maintained architecture
docs, add a design changelog entry, and optionally add a short note at the top
of the affected TDD pointing to the newer source of truth.

## Generated Docs Intent

Public headers should gain concise Doxygen comments as the API becomes real.
Once the API surface is large enough to justify generated reference docs, add a
CMake-driven Doxygen target. If the project needs a polished public docs site,
layer Sphinx with Breathe/Exhale over Doxygen XML so authored docs and API
reference publish together.
