# tess Documentation

This tree separates current project documentation from historical design
intent.

## Current Docs

- [Getting Started](getting-started.md): tutorial from shapes and schemas
  to the schedule loop and render bridge.
- [Architecture](architecture/README.md): maintained design notes that should
  track implementation.
- [Performance](performance.md): benchmark trend snapshots and calibration
  workflow.
- [Dependencies](dependencies.md): external library choices and documentation.
- [Style](style.md): C++ coding style and formatting policy.
- [Git Hooks](git-hooks.md): local commit and push guardrails.

## Internal Records

- [Planning](planning/README.md): milestone plans and audit records;
  internal working documents, not a guide to current behavior.
- [Optimization Log](planning/optimization-log.md): accepted, rejected, and
  deferred performance experiments.

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

## Generated Docs Intent

Public headers should gain concise Doxygen comments as the API becomes real.
Once the API surface is large enough to justify generated reference docs, add a
CMake-driven Doxygen target. If the project needs a polished public docs site,
layer Sphinx with Breathe/Exhale over Doxygen XML so authored docs and API
reference publish together.
