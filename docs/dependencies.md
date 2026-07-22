# Dependencies

## GoogleTest

- Version: `v1.17.0`
- Documentation: https://google.github.io/googletest/
- CMake quickstart:
  https://google.github.io/googletest/quickstart-cmake.html
- Release: https://github.com/google/googletest/releases/tag/v1.17.0

Used for C++ unit tests.

Developer builds fetch the SHA-pinned source by default. Setting
`TESS_USE_SYSTEM_DEPENDENCIES=ON` instead requires a CMake package at version
1.17.0 or newer and fails configuration if the expected imported target is
missing. As required for composable `add_subdirectory` use, a canonical
`GTest::gtest_main` target already provided by a parent project takes
precedence as an explicit injection/trust boundary; tess does not reinterpret
directory-scoped version variables, so the parent owns its compatibility.

## Google Benchmark

- Version: `v1.9.5`
- Documentation: https://google.github.io/benchmark/
- Repository and releases: https://github.com/google/benchmark

Used for opt-in C++ benchmarks.

Benchmark builds fetch the SHA-pinned source by default. With
`TESS_USE_SYSTEM_DEPENDENCIES=ON`, configuration requires Google Benchmark
1.9.5 or newer. A parent-provided `benchmark::benchmark_main` target follows
the same explicit trust rule and bypasses tess's package-version validation.

On macOS, benchmark configure or execution may warn that pthread affinity or CPU
frequency metadata is unavailable. Those warnings do not prevent benchmark
measurements from running.

## Doxygen

- Documentation: https://www.doxygen.nl/manual/
- Repository and releases: https://github.com/doxygen/doxygen

Optional, docs-only tool dependency for the opt-in `tess_docs` target
(`TESS_BUILD_DOCS=ON`), which generates a local HTML API reference via
CMake's `doxygen_add_docs`. Nothing in the library, tests, benchmarks,
or normal CI requires it; `find_package(Doxygen REQUIRED)` runs only when the
option is enabled. Local builds accept the installed version and are developed
against Doxygen 1.17.0. The Pages workflow pins the official 1.17.0 Linux
binary and verifies its published SHA-256 digest before extraction.
Documentation-only `DOXYGEN_PREDEFINED` gates make the EnTT adapter,
diagnostics, and ImGui panel APIs visible in the reference without their
third-party headers. The generated reference excludes `tess::detail`, omits
per-member missing-comment warnings to match the repository's namespace-scope
comment policy, and fails on remaining Doxygen warnings before its HTML is
copied under `/api/` in the combined Pages artifact.

## Documentation site

- MkDocs version: `1.6.1`
- Material for MkDocs version: `9.7.7`
- MkDocs documentation: https://www.mkdocs.org/
- Material documentation: https://squidfunk.github.io/mkdocs-material/
- Package releases: https://pypi.org/project/mkdocs-material/

The authored public site is built from `mkdocs.yml` and deployed as a static
GitHub Pages artifact. `requirements-docs.in` pins the direct theme dependency;
`requirements-docs.txt` locks all transitive packages and distribution hashes.
The site remains on MkDocs 1.6.1; MkDocs 2.0 is not an automatic upgrade because
its current design is incompatible with the existing theme and plugin model.

Architecture diagrams use Material for MkDocs' native Mermaid integration:
https://squidfunk.github.io/mkdocs-material/reference/diagrams/. Mermaid is not
a separate Python or build dependency; the pinned Material theme owns the
browser runtime and initializes it only on pages containing a `mermaid` fence.
The native integration follows the site's fonts and light/dark palettes and
works with instant navigation.

Regenerate the docs lock with uv 0.11.29:

```sh
tools/compile_docs_requirements.sh
```

## Logo typeface

The tess wordmark contains static vector outlines derived from Sirenia Light,
designed by Felix Braden and published by Floodfonts:
https://fonts.adobe.com/fonts/sirenia. Adobe Fonts permits licensed users to
create and distribute images and vector artwork, including logos, and to
modify type after conversion to outlines:
https://helpx.adobe.com/fonts/using/font-licensing.html.

The repository does not distribute the font software. It contains only the
finished outline paths. The static SVGs remain reproducible and distributable
independently of an Adobe Fonts subscription. Anyone who needs to regenerate
or edit the lettering through the typeface must obtain their own Adobe Fonts
or desktop font license.

## Emscripten

- Version: `6.0.3`
- Documentation: https://emscripten.org/docs/
- SDK repository: https://github.com/emscripten-core/emsdk
- Official container: https://hub.docker.com/r/emscripten/emsdk
- Official container digest:
  `emscripten/emsdk:6.0.3@sha256:bb0910e6a18bb9bd7cb31ae4ed40f9073148b78cb2cdb8ea8676454e0d85425c`

Emscripten builds only the interactive documentation example; it is not a
library dependency. CI pulls the upstream project's multi-platform image by
immutable manifest digest rather than executing a third-party setup action.
The demo is single-threaded, uses no filesystem, and compiles the same
pathfinding headers as the native self-checking model.

## Dear ImGui

- Documentation: https://github.com/ocornut/imgui (README, `docs/`, wiki)
- Repository and releases: https://github.com/ocornut/imgui

Optional, consumer-provided integration dependency for the header-only reference
panels in `include/tess/debug/imgui/panels.h`. tess core never fetches, links,
or requires ImGui: the panels header compiles only when the consumer defines
`TESS_ENABLE_IMGUI`, and the consumer supplies its own Dear ImGui and includes
`<imgui.h>` before the header (a `#error` enforces the order). Only the stable
core API is used -- `ImGui::Text`, `ImGui::TextUnformatted`, `ImGui::Separator`,
and the `IMGUI_VERSION` macro -- so no specific version is pinned; any recent
Dear ImGui works. Known-compatible with ocornut/imgui `8936b58`. tess CI
validates the header against a minimal stub (`tests/imgui_stub/imgui.h`,
`tess_diagnostics_panels_test`) rather than the real library, so tess builds add
no ImGui dependency.

## EnTT

- Version: `v3.16.0` (SHA-pinned in `cmake/TessEnttDeps.cmake` to
  `b4e58bdd364ad72246c123a0c28538eab3252672`; latest upstream tag as of
  2026-07-10)
- Documentation: https://github.com/skypjack/entt (README, wiki) and
  https://skypjack.github.io/entt/
- Repository and releases: https://github.com/skypjack/entt

Optional integration dependency for the EnTT adapter in
`include/tess/ecs/entt/entt_adapter.h`. tess core never fetches, links, or
requires EnTT; two independent gates exist and both matter:

- `TESS_ENABLE_ENTT` as a **preprocessor macro** is the consumer-side header
  gate (the ImGui precedent): the adapter header compiles to nothing without
  it, and the consumer supplies EnTT and includes
  `<entt/entity/registry.hpp>` before the header (an `#error` on the
  `ENTT_VERSION` macro enforces the order). The macro must be defined
  per-target (`target_compile_definitions(... PRIVATE TESS_ENABLE_ENTT)`),
  never globally.
- `TESS_ENABLE_ENTT` as a **CMake option** (default `OFF`, `ON` in the
  `dev`, `release`, `bench`, and `windows-msvc` presets) gates only tess's
  own EnTT-dependent test, example, and benchmark targets, which acquire
  real EnTT through `tess_require_entt()`. The default dependency mode uses
  `FetchContent` at the pinned SHA (`SYSTEM`/`EXCLUDE_FROM_ALL`);
  `TESS_USE_SYSTEM_DEPENDENCIES=ON` instead requires EnTT 3.16.0 or newer.
  The feature default stays `OFF` so ordinary consumer builds never fetch.
  A parent-provided `EnTT::EnTT` target takes precedence as a documented trust
  boundary and bypasses tess's package-version validation.

EnTT requires C++17; tess builds it under `cxx_std_20`. The pinned SHA is
exercised by the repository's required platform matrix. The dependency-free
concepts layer
(`include/tess/ecs/adapter.h`) is always built and tested without EnTT.

## GitHub Actions

- Checkout action version: `actions/checkout@v7.0.0` (pinned to
  `9c091bb21b7c1c1d1991bb908d89e4e9dddfe3e0`)
- Checkout documentation: https://github.com/actions/checkout
- Cache action version: `actions/cache@v6.1.0` (pinned to
  `55cc8345863c7cc4c66a329aec7e433d2d1c52a9`)
- Cache documentation: https://github.com/actions/cache
- Upload artifact action version: `actions/upload-artifact@v7.0.1` (pinned to
  `043fb46d1a93c77aae656e7c1c64a875d1fc6a0a`)
- Upload artifact documentation: https://github.com/actions/upload-artifact
- Setup Python action version: `actions/setup-python@v7.0.0` (pinned to
  `5fda3b95a4ea91299a34e894583c3862153e4b97`)
- Setup Python documentation: https://github.com/actions/setup-python
- Hosted runner documentation:
  https://docs.github.com/actions/reference/runners/github-hosted-runners
- Configure Pages action version: `actions/configure-pages@v6.0.0` (pinned to
  `45bfe0192ca1faeb007ade9deae92b16b8254a0d`)
- Upload Pages artifact action version:
  `actions/upload-pages-artifact@v5.0.0` (pinned to
  `fc324d3547104276b827a68afc52ff2a11cc49c9`)
- Deploy Pages action version: `actions/deploy-pages@v5.0.0` (pinned to
  `cd2ce8fcbc39b97be8ca5fce6e763baed58fa128`)
- Pages custom-domain documentation:
  https://docs.github.com/pages/configuring-a-custom-domain-for-your-github-pages-site

CI selects explicit OS-family labels — `ubuntu-24.04`, `macos-15`, and
`windows-2025` — instead of `-latest` labels. This avoids automatic OS-family
migrations, but GitHub refreshes each hosted image in place, so its compilers,
CMake, and preinstalled tools still roll. GitHub currently documents the
public x64 Ubuntu runner as four CPUs with 16 GB of RAM; the clang-tidy cap
matches those CPUs. The jobs also install `ccache` and `clang-tidy` from live
apt or Homebrew repositories; their resolved versions are reported but not
pinned. Benchmark baseline JSON is uploaded from CI artifacts so timing
thresholds can be calibrated against the same runner family that will enforce
them; benchmark gates therefore run only on the Linux runner family they were
calibrated on. Every checkout disables persisted Git credentials because these
jobs only need repository read access.

## tiktoken

- Version: `0.13.0`
- Documentation and releases: https://pypi.org/project/tiktoken/
- Repository: https://github.com/openai/tiktoken

Used by the Git pre-commit hook to count tokens in staged text files through
the Python API. The hook reads staged blobs from Git, so the library API is a
better fit than a filesystem-oriented command-line wrapper.

## Python Development Tools

- uv version: `0.11.28` (latest upstream release as of 2026-07-12)
- uv documentation: https://docs.astral.sh/uv/
- pytest version: `9.1.1`
- pytest documentation: https://docs.pytest.org/
- clang-format Python package version: `22.1.5`
- clang-format documentation: https://clang.llvm.org/docs/ClangFormat.html
- uv requirements locking:
  https://docs.astral.sh/uv/pip/compile/
- uv isolated command execution:
  https://docs.astral.sh/uv/reference/cli/#uv-run

`requirements-dev.in` holds the three direct tool pins.
`requirements-dev.txt` is a universal `uv pip compile` result containing exact
transitive versions, environment markers, and distribution hashes; its header
records the checked-in regeneration wrapper. CI uses GitHub's SHA-pinned
`setup-python` action, creates `.venv`, and installs the lock with
`pip --require-hashes`. Subsequent checks execute Python, pytest, and
clang-format from that exact environment. uv remains the local, version-pinned
lockfile generator but is not executed as a GitHub Action.

Regenerate the hash lock with uv 0.11.28:

```sh
tools/compile_requirements.sh
```

The wrapper checks the uv version, enables upgrades, fixes the package-index
cutoff at `2026-07-13T00:00:00Z`, and supplies a stable custom header. Pass an
optional output path to generate a comparison lock without replacing the
checked-in file. Advance the cutoff deliberately when refreshing dependency
pins.

The lock includes hashes for every published artifact uv considers, so it
remains portable across supported Python versions and platforms. The current
lock is 30,877 bytes and 16,038 GPT-5 tokens, below the repository file limit.

## CMake clang-tidy Integration

- Documentation:
  https://cmake.org/cmake/help/latest/variable/CMAKE_LANG_CLANG_TIDY.html
- Target property:
  https://cmake.org/cmake/help/latest/prop_tgt/LANG_CLANG_TIDY.html
- Build-tool parallelism:
  https://cmake.org/cmake/help/latest/manual/cmake.1.html#build-a-project

Used by the opt-in `dev-clang-tidy` preset through the `CXX_CLANG_TIDY` target
property. Tess sets the property only on local example, test, and benchmark
targets so third-party targets are not linted by project policy. Required CI
caps this analysis build at four concurrent jobs; an explicit `--parallel 4` is
portable across CMake generators and avoids unbounded runner memory pressure.

## clangd

- Configuration documentation: https://clangd.llvm.org/config
- Feature documentation: https://clangd.llvm.org/features#clang-tidy-checks

Used for editor diagnostics and navigation. Start clangd with
`--clang-tidy --enable-config`; the checked-in `.clangd` points clangd at the
default developer compilation database in `build/dev`, and `.clang-tidy`
selects the clang-tidy checks.

The `dev-clang-tidy` preset is a CI quality gate for low-noise
clang-analyzer, bugprone, performance, and selected readability checks. The
`dev-clang-tidy-advisory` preset uses `.clang-tidy-advisory` for broader noisy
checks that should be reviewed but are not yet part of the blocking gate. A
weekly scheduled workflow runs the advisory preset, and maintainers can also
start it manually. Current advisory findings include known style debt such as
redundant `typename` and swappable coordinate parameters. Promote advisory
checks only after those findings are either fixed or intentionally suppressed.

## Cppcheck

- Version: `2.21.0`
- Manual: https://cppcheck.sourceforge.io/manual.html
- CMake target property:
  https://cmake.org/cmake/help/latest/prop_tgt/LANG_CPPCHECK.html

Used by the opt-in `dev-cppcheck` preset through the `CXX_CPPCHECK` target
property. Tess sets the property only on local test and benchmark targets so
third-party targets are not analyzed by project policy. The preset enables
`warning` and `portability` checks; cppcheck `style` and `performance` checks
are intentionally deferred because early runs mostly report low-signal advice
for small value types and static member functions in this template-heavy API.
The preset narrowly suppresses cppcheck `internalError` for
`include/tess/core/shape.h`, where cppcheck 2.21.0 fails while analyzing
`ShapeTraits` non-type template parameter constants. The queued-operation
planner uses an inline `returnDanglingLifetime` suppression where cppcheck
reports a false positive for a pointer to an element inside a caller-provided
span. The preset also suppresses `syntaxError` for the diagnostics macro test
translation units because cppcheck misparses GoogleTest `TEST` macros there;
those files remain covered by normal, warnings-as-errors, and sanitizer builds.

## Clang Sanitizers

- AddressSanitizer documentation:
  https://clang.llvm.org/docs/AddressSanitizer.html
- UndefinedBehaviorSanitizer documentation:
  https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html

Used by the opt-in `dev-asan` preset for tests. Tess applies sanitizer compile
and link flags to local executables only, because AddressSanitizer must be
linked into the final executable.

## Steam Runtime SDK

- Image: `registry.gitlab.steamos.cloud/steamrt/steamrt4/sdk`
- Pinned digest:
  `sha256:584939ebd7d2f1eec719e771fdde4ae3bd469ee741c783abb7fe812ddaaf3ee4`
- Documentation: https://gitlab.steamos.cloud/steamrt/steamrt4/sdk
- Valve runtime guide:
  https://gitlab.steamos.cloud/steamrt/steam-runtime-tools/-/blob/main/docs/container-runtime.md

The Steam Deck tooling builds and optionally runs inside this immutable SDK
image so a mutable tag cannot silently change the compiler/sysroot. Developers
may deliberately test another image with `TESS_STEAMRT_IMAGE`; the override is
propagated to both the local container build and on-device container path.
The pinned SDK already supplies Clang 19, so the wrapper performs no live
`apt-get` step that could drift outside the image digest.

## Deferred Executor Candidates

No block-executor dependency is added yet. `work_contract`, Taskflow, oneTBB,
and enkiTS remain candidates for later executor backends after Tess has an
internal executor abstraction and benchmark comparisons.

Public documentation inspected for the deferred Building C++ candidates:

- `work_contract`: https://github.com/buildingcpp/work_contract
  - Pinned research commit:
    `3f56a17e36db57846a086e20d8788478287f3c86`
  - Commit URL: [work_contract pinned commit][work-contract-commit]
  - PDF overview:
    https://www.buildingcpp.com/documents/work_contract.pdf
  - CppCon 2024 talk:
    https://www.youtube.com/watch?v=oj-_vpZNMVw
  - CppCon 2025 talk:
    https://www.youtube.com/watch?v=5ghAa7B5bF0
- `signal_tree`: https://github.com/buildingcpp/signal_tree
  - Pinned research commit:
    `f7b59510e117bc6156af86a6b8689ca4a3832e3c`
  - Commit URL: [signal_tree pinned commit][signal-tree-commit]

As of the 2026-06-08 spike, `signal_tree` is a readiness-selection primitive
that stores signal ids instead of work payloads. It does not provide phase
completion, result reduction, worker lifetime, or dirty-merge semantics.
`work_contract` adds recurrent task lifecycle semantics, coalesced scheduling,
blocking and non-blocking groups, async release, and exception callbacks. That
is closer to deferred maintenance scheduling than scoped phase execution, but
still stronger than the current Tess executor adapter needs. Neither library
is adopted yet.

[work-contract-commit]: https://github.com/buildingcpp/work_contract/commit/3f56a17e36db57846a086e20d8788478287f3c86
[signal-tree-commit]: https://github.com/buildingcpp/signal_tree/commit/f7b59510e117bc6156af86a6b8689ca4a3832e3c

## Documentation assets (vendored)

- `doxygen-awesome-css` `v2.4.2` — MIT-licensed stylesheet set for the
  Doxygen API reference. Vendored with checksums in
  [`docs/doxygen-awesome/`](doxygen-awesome/README.md).
  Documentation: https://jothepro.github.io/doxygen-awesome-css/
- Fraunces (variable, latin subset) — OFL-licensed heading font for the
  MkDocs site, vendored in [`docs/assets/fonts/`](assets/fonts/README.md).
  Upstream: https://github.com/undercasetype/Fraunces
