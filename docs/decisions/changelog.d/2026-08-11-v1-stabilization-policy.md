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
- Compatibility snapshots retain normalized declarations rather than treating
  symbol-name retention as source compatibility. Released snapshot bytes are
  anchored to their `v<version>` tag, with path confinement and immutability
  checked on ordinary changes. Their named consumer targets discover and link
  the candidate installation through supported CMake package entry points,
  and release CI builds and runs each named test explicitly.
- Direct aggregate imports remain unconditional, and existing stable types
  retain aggregate status, including aggregates with public non-virtual bases,
  and cannot gain public data members or inherited constructors because those
  changes can break aggregate or structured-binding consumers. Conditional
  declarations, access labels, enums, and enumerators keep their enclosing C++
  scope and receive branch identities so they cannot evade the same checks;
  aggregate eligibility and implicit enumerator positions are retained across
  conditional alternatives, including branch-specific visibility. Declarator
  recognition distinguishes parenthesized type/specifier syntax from callable
  parameters, and attributed callables retain their actual callable identity.
  Redefining or undefining a snapshotted stable macro is a compatibility break.
  Constructor recognition covers attributes, function-like or object-like
  annotation macros before or after the declared name, and conditional
  `explicit` specifiers without mistaking parameter-type spellings or
  destructors for constructors. Callable identity likewise retains annotated
  names, distinguishes destructors, and treats named or operator base overloads
  imported through using-declarations as overload additions. Conditional
  aggregate-breaking member and base configurations retain distinct identities,
  so narrowing the configurations in which a type remains an aggregate is
  rejected; private anonymous type objects remain aggregate-breaking data.
  Enumerator order is append-only, and existing callables cannot gain overloads
  because calls and address-taking can become ambiguous; sparse extensions
  therefore use distinctly named entry points. Release tags anchor an
  append-only snapshot-directory inventory as well as every snapshot byte on
  the current history; future unmerged release tags do not constrain older
  maintenance branches. A
  newly required snapshot must exactly match the current inventories before
  becoming immutable. Prerelease package configs export numeric, prerelease,
  and full version metadata even though discovery must be unversioned.
- The release evidence JSON records the expected/pinned toolchain contract and
  checksums retained copies of the successful platform-job logs containing the
  actual current and floor tool versions. The workflow-run URL remains
  supplemental provenance; expected floors are not mislabeled as observations,
  and missing MSVC metadata or a version other than 19.44 fails the floor job.
