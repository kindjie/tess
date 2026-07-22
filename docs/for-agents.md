# Adopting tess programmatically

tess is unusually verifiable for automated adopters (coding agents, CI
bots, scripted evaluations): the documentation is contract-checked
against compiled code, every example is a self-checking binary with
pinned output, and the simulation is deterministic. This page is the
minimal adoption recipe.

## Install and verify

Consume the library per the [installation guide](packaging.md) —
`FetchContent` pinned to the release tag is the one-block path. Then
verify the toolchain end to end with the dependency-free examples:

```sh
cmake --preset examples
cmake --build --preset examples
./build/examples/examples/tess_quickstart
```

Expected stdout, byte-exact (CI enforces this against
`examples/quickstart.cc`):

```text
path cost: 14
expanded nodes: 15
```

Every other example binary (`tess_<name>`) exits `0` on success and
nonzero with a diagnostic on failure, so a build-and-run sweep of
`build/examples/examples/` is a complete integration check.

## Trust model for generated code

- **Snippets are ground truth.** Every C++ block in these docs sits in a
  `tess-snippet` region byte-matched against a compiled, self-checking
  source file by `tools/check_doc_snippets.py`. Copying a documented
  snippet cannot copy drifted code.
- **The include surface is contracted.** Consume only
  `<tess/pathfinding.h>`, `<tess/simulation.h>`, or the `<tess/tess.h>`
  umbrella; [installation](packaging.md) documents the supported
  surface, and `tools/check_public_surface.py` enforces it.
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
