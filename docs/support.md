# Support and compatibility

Use [GitHub Discussions](https://github.com/kindjie/tess/discussions) for
adoption questions and design conversations. Use
[GitHub Issues](https://github.com/kindjie/tess/issues) for reproducible bugs
and focused feature requests. Security reports should follow the repository's
[security policy](https://github.com/kindjie/tess/security/policy) rather than
being filed publicly.

All `0.x` releases are pre-stable. Public APIs and data layouts may change
between minor versions while the design is validated. Release tags are the
supported consumption points; pin a tag or commit rather than a branch. The
[roadmap](roadmap.md) records what is shipped, deferred, and out of scope.

## The 1.x compatibility contract

Within the 1.x release line, tess preserves source compatibility for:

- documented APIs declared by `stable` and `optional-stable` headers;
- documented configuration macros;
- the `tess::tess` CMake target and installed-package entry points;
- direct membership of the stable aggregate headers; and
- archive format v1, including loading fixtures produced by earlier 1.x
  releases.

Direct aggregate membership remains unconditional. Existing aggregate types
remain aggregates, and public data-member sets on existing stable types remain
fixed, so constructors, hidden state, or fields cannot break aggregate or
structured-binding users. Enumerator order is append-only. New overloads of an
existing callable are excluded because they can make existing calls and
address-taking ambiguous; additive behavior uses a distinctly named method,
type, or entry point.

`cmake/tess-headers.json` is the machine-readable source of truth for header
stability. Every installed header has exactly one classification:

- `stable` headers form the dependency-free supported surface;
- `optional-stable` headers are supported when the named third-party
  integration is enabled;
- `experimental` headers are installed for evaluation but may change or be
  removed in any release; and
- `implementation-only` headers are installed only because public templates
  depend on them and must not be included directly.

The contract does not cover ABI or object layout, binary compatibility,
mixing tess versions or build-wide configurations across translation units,
implementation names, or identity-token comparisons across dynamic-library
boundaries. Rebuild all consuming translation units together with one tess
version and one macro configuration.

The current dense signatures for queued operations, field products, and PIBT
are part of the stable source contract. Sparse support must arrive through new
entry points rather than changing those signatures or adding ambiguous
overloads.

Compatibility is enforced in layers. Snapshots protect header classes, direct
aggregate membership, and documented public namespace-scope names. Installed
and optional-integration builds, immutable source-consumer fixtures, archive
fixtures, and release review cover C++ semantics. The snapshot checker
deliberately does not parse declaration signatures or model C++ semantics and
is not represented as a complete proof of this policy.

## Toolchain floors

The required language baseline is C++20. Release evidence continuously builds
and runs the stable surface with:

- GCC 12 and Clang 16 on Ubuntu 24.04;
- AppleClang from Xcode 16.0 on macOS 15;
- MSVC 19.44 from Visual Studio 2022 17.14; and
- CMake 3.25.3 through configure, install, package discovery, consumer build,
  and execution.

Current compiler and CMake versions are tested separately. Stable surfaces are
also compiled and run without RTTI on every compiler family, using `-fno-rtti`
or `/GR-`. Exact release cells and their results are recorded in the release
evidence artifact, which bundles checksummed job logs recording the actual tool
versions for each platform cell and retains the workflow-run URL as provenance.
