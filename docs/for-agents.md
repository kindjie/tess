# Adopting tess programmatically

tess is unusually verifiable for automated adopters (coding agents, CI
bots, scripted evaluations): the documentation is contract-checked
against compiled code, every example is a self-checking binary (the
quickstart's stdout is additionally pinned byte-exact), and the
simulation is deterministic. This page is the minimal adoption recipe.

## Install and verify

Consume the library per the [installation guide](packaging.md).
`FetchContent` pinned to the release tag is the one-block CMake path. For a
compiler-only environment, download the release's portable headers archive and
`SHA256SUMS`, verify it, extract it, and add `<root>/include` to the compiler
search path. That asset contains the same installed headers tested by the CMake
package, including its concrete `tess/version.h`; a raw source archive does
not. Then verify the toolchain end to end with the dependency-free examples:

```sh
cmake --preset examples
cmake --build --preset examples
./build/examples/examples/tess_quickstart
```

Expected stdout, byte-exact (CI enforces this against
`examples/quickstart.cc`):

<!-- tess-output: quickstart source=examples/quickstart.cc -->
```text
path: [Coord3{0, 0, 0}, Coord3{1, 0, 0}, Coord3{2, 0, 0}, Coord3{2, 1, 0}]
cost: 3
```
<!-- /tess-output -->

Every other example binary (`tess_<name>`) exits `0` on success and
nonzero with a diagnostic on failure, so a build-and-run sweep of
`build/examples/examples/` is a quick smoke test of the toolchain and
library together. (The EnTT and Flecs adapter examples need the `dev` preset,
and installed-package consumption is verified separately — see
[installation](packaging.md).)

## Trust model for generated code

- **Snippets are ground truth.** Every C++ block in these docs sits in a
  `tess-snippet` region byte-matched against a compiled, self-checking
  source file by `tools/check_doc_snippets.py`. Copying a documented
  snippet cannot copy drifted code.
- **Header support follows the stability manifest.** Include the
  `<tess/tess.h>` umbrella, one of the aggregates such as
  `<tess/pathfinding.h>` or `<tess/simulation.h>`, or the narrowest header
  that owns the API -- the last is preferable in compile-sensitive code,
  and is what the quickstart does. Stable and optional-stable headers are the
  supported 1.x source surface; experimental and implementation-only headers
  are not. See [installation](packaging.md). What
  `tools/check_public_surface.py` gates is the symbol MANIFEST, not an
  include policy: every namespace-scope public name must appear in
  `docs/architecture/surface.json`, so a symbol cannot ship undocumented.
- **Determinism enables self-checks.** Fixed-step ticks with identical
  inputs reproduce identical outputs, so generated integration tests can
  assert exact costs, orders, and versions rather than tolerances.
- **Unshipped features are labeled.** The [roadmap](roadmap.md) lists
  designed-but-deferred APIs; do not generate code against them.

## Route by task

- Choosing APIs for a workload: [getting-started](getting-started.md),
  then the [pathfinding note](architecture/path.md).
- Semantics and contracts: the [Concepts pages](architecture/README.md)
  are normative.
- Failure triage: examples print a diagnostic before a nonzero exit;
  compile-time schema and concept errors are designed to name the
  violated requirement.
