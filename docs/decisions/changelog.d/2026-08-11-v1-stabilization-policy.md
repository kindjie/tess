## 2026-08-11 - Define the enforceable 1.x stability boundary

- Decided: one exhaustive header manifest classifies installed headers as
  stable, optional-stable, experimental, or implementation-only. CMake derives
  installation file sets from it, while `surface.json` remains a symbol
  documentation inventory rather than being repurposed as a compatibility
  manifest. Stable aggregates may not directly import either excluded class;
  this removes both maintenance and `path/node_index_space.h` from the main
  umbrella.
- The 1.x contract covers documented source APIs, configuration macros, CMake
  package entry points and targets, stable aggregate membership, and archive
  v1. It excludes ABI, object layout, mixed versions or configurations across
  translation units, implementation names, and cross-DSO comparison of
  process-local type identities. Public identity-bearing caches, graphs,
  payloads, and products state that exclusion directly.
- Worker-pool nested or concurrent dispatch and reservation during dispatch
  fail fast in release as well as debug builds under the existing once-per-call
  mutex. Detectable misuse must not remain able to corrupt shared state merely
  because assertions were compiled out. A dispatch keeps that ownership until
  its plan-ordered result has been copied from shared storage under the mutex.
  Even an empty nested dispatch performs the once-per-call misuse check.
  Both threaded executor variants are stable; callback-state synchronization,
  join, allocation, result-order, and worker-count contracts remain unchanged.
- CMake prereleases carry an explicit label and full version string. An
  unversioned package lookup may select an RC, while every versioned request is
  rejected. Stable 0.x packages use same-minor selection and stable 1.x
  packages use same-major selection, preventing a numerically equal RC from
  satisfying a request for stable 1.0.0.
- The in-tree vcpkg overlay remains checkout-based. A release archive cannot
  contain its own final hash, so central-registry metadata and the archive hash
  are post-release publication work rather than self-fetching 1.0 gates. The
  Conan recipe and checkout-based vcpkg overlay are instead tested through
  consuming executables. Their release job clears the workflow-level compiler
  launcher because the hosted image has no `ccache`, and Conan creation pins
  the supported C++20 language mode rather than accepting its detected C++17
  default.
- Dense queued-operation, field-product, and PIBT signatures freeze for 1.x;
  sparse support must be additive. Breaking argument-pair, options, handle,
  ordering, duplicate-name, lifetime, and identity cleanups land in v0.13 and
  are documented in the 1.0 upgrade guide.
- Compatibility snapshots retain header classes, direct aggregate membership,
  and documented public namespace-scope names. They do not parse C++
  declarations. Signatures, defaults, aggregate use, fields, overload
  resolution, and configuration-selected APIs remain protected by immutable
  compiled consumers, integration builds, and release review. The name
  inventory is one evidence layer, not a complete proof of source
  compatibility. Direct aggregate imports remain unconditional and sparse
  extensions use distinctly named entry points rather than ambiguous overloads.
- Released snapshot bytes are anchored to their `v<version>` tag, with path
  confinement and immutability checked on ordinary changes. Their named
  consumer targets discover and link the candidate installation through
  supported CMake package entry points, and release CI builds and runs each
  named test explicitly. Release tags anchor the append-only snapshot-directory
  inventory; future unmerged tags do not constrain maintenance branches. A new
  required snapshot must exactly match current inventories before becoming
  immutable. Prerelease package configs export numeric, prerelease, and full
  version metadata even though discovery must be unversioned.
- The release evidence JSON records the expected/pinned toolchain contract and
  checksums retained copies of the successful platform-job logs containing the
  actual current and floor tool versions. The workflow-run URL remains
  supplemental provenance; expected floors are not mislabeled as observations,
  and missing MSVC metadata or a version other than 19.44 fails the floor job.
