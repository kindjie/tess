# Dependencies

## CMake release floor

- Version: `3.25.3`
- Documentation: https://cmake.org/cmake/help/v3.25/
- Release files and checksums: https://cmake.org/files/v3.25/

The release-floor job downloads Kitware's Linux x86-64 archive and verifies
the URL and SHA-256 digest pinned in `ci/tools.lock.json`. It performs a real
configure, install, package discovery, consumer build, and execution rather
than relying on syntax compatibility alone.

## ccache CI binary

- Version: `4.13.6`
- Documentation: https://ccache.dev/manual/4.13.6.html
- Release files and signatures: https://ccache.dev/download.html

Linux CI downloads the upstream x86-64 musl-static archive using the URL and
SHA-256 digest pinned in `ci/tools.lock.json`. The archive is a CI acceleration
tool, not a library build or runtime dependency. The installer accepts only
HTTPS redirects, bounds each transfer attempt and the retry scheduling window,
checks the digest before extraction, and verifies the executed version.

## GoogleTest

- Version: `v1.17.0`
- Documentation: https://google.github.io/googletest/
- CMake quickstart:
  https://google.github.io/googletest/quickstart-cmake.html
- Release: https://github.com/google/googletest/releases/tag/v1.17.0

Used for C++ unit tests.

Developer builds shallow-fetch the SHA-pinned source by default. The complete
fetch and checkout sequence is retried up to three times, and the fetched and
checked-out revisions are verified before use. Setting
`TESS_USE_SYSTEM_DEPENDENCIES=ON` instead requires a CMake package at version
1.17.0 or newer and fails configuration if the expected imported target is
missing. As required for composable `add_subdirectory` use, a canonical
`GTest::gtest_main` target already provided by a parent project takes
precedence as an explicit injection/trust boundary; tess does not reinterpret
directory-scoped version variables, so the parent owns its compatibility.

## Conan

- Version: `2.31.1`
- Documentation: https://docs.conan.io/2/
- Package and release files: https://pypi.org/project/conan/2.31.1/

Used only by the release package-validation job. The selected wheel URL and
SHA-256 digest are pinned in `ci/tools.lock.json`; the library itself has no
runtime or build dependency on Conan.

## Google Benchmark

- Version: `v1.9.5`
- Documentation: https://google.github.io/benchmark/
- Repository and releases: https://github.com/google/benchmark

Used for opt-in C++ benchmarks.

Benchmark builds use the same retrying, exact-revision shallow fetch by
default. With
`TESS_USE_SYSTEM_DEPENDENCIES=ON`, configuration requires Google Benchmark
1.9.5 or newer. A parent-provided `benchmark::benchmark_main` target follows
the same explicit trust rule and bypasses tess's package-version validation.

On macOS, benchmark configure or execution may warn that pthread affinity or CPU
frequency metadata is unavailable. Those warnings do not prevent benchmark
measurements from running.

## Proposed external grid benchmark data (not adopted)

- Repository: https://bitbucket.org/shortestpathlab/benchmarks
- Proposed revision: `fe6351b0700a0f4e75d0bd79ce3bf5478bc60c94`
- Format and source documentation:
  https://www.movingai.com/benchmarks/formats.html and
  https://www.movingai.com/benchmarks/grids.html
- Database license: Open Data Commons Attribution License 1.0
  (https://opendatacommons.org/licenses/by/1-0/)
- Design: `docs/tdd/grid-benchmark-data-and-scenario-oracle.md`

The revision was verified through Bitbucket's commit API on 2026-07-22. This
is a proposed opt-in test-data source, not a library or build dependency. No
external files are in the repository, no downloader is enabled, and no CI job
requires the data. ODC-By covers the database but excludes copyright in
individual contents; acquisition remains blocked until the initial synthetic
maps and scenarios have a documented content-rights basis. When that gate
clears, the required Sturtevant 2012 citation and ODC-By produced-work notice
must accompany published results.

## Doxygen

- Minimum version: `1.17.0`
- Documentation: https://www.doxygen.nl/manual/
- Repository and releases: https://github.com/doxygen/doxygen

Optional, docs-only tool dependency for the opt-in `tess_docs` target
(`TESS_BUILD_DOCS=ON`), which generates a local HTML API reference via
CMake's `doxygen_add_docs`. Nothing in the library, tests, benchmarks,
or normal CI requires it; `find_package(Doxygen 1.17 REQUIRED)` runs only when
the option is enabled. Local builds require Doxygen 1.17.0 or newer because
the complete layout uses that schema. The Pages workflow pins the official
1.17.0 Linux binary and verifies its published SHA-256 digest before
extraction.
Documentation-only `DOXYGEN_PREDEFINED` gates make the EnTT and Flecs
adapters, diagnostics, ImGui panels, and WebGPU APIs visible in the reference
without their third-party headers. The generated reference excludes
`tess::detail`, omits per-member missing-comment warnings to match the
repository's namespace-scope comment policy, and fails on remaining Doxygen
warnings before its HTML is copied under `/api/` in the combined Pages
artifact. `docs/doxygen-layout.xml` is the complete warning-clean layout for
the pinned Pages version, extended with relative Docs, Learn, and Reference
tabs. `tools/check_doxygen_navigation.py` validates those generated menu links
and representative API pages before publication.

## Documentation site

- MkDocs version: `1.6.1`
- Material for MkDocs version: `9.7.7`
- mike version: `2.2.0` (versioned deployment: one tree per documented
  version plus the `latest` alias)
- MkDocs documentation: https://www.mkdocs.org/
- Material documentation: https://squidfunk.github.io/mkdocs-material/
- mike documentation: https://github.com/jimporter/mike
- Package releases: https://pypi.org/project/mkdocs-material/

The authored public site is built from `mkdocs.yml` and deployed as a static
GitHub Pages artifact. `requirements-docs.in` pins the direct theme dependency;
`requirements-docs.txt` locks all transitive packages and distribution hashes.
The site remains on MkDocs 1.6.1; MkDocs 2.0 is not an automatic upgrade because
its current design is incompatible with the existing theme and plugin model.

Architecture diagrams use Material for MkDocs' native Mermaid integration:
https://squidfunk.github.io/mkdocs-material/reference/diagrams/. The browser
runtime is a separate pinned build-time dependency: `tools/fetch_mermaid.py`
downloads Mermaid 11.16.1 from the npm registry, verifies pinned SHA-256
digests, and places it in `docs/assets/javascripts/` (gitignored) so the site
serves it from its own origin instead of the theme's unpkg.com fallback.
`overrides/main.html` exposes a narrow lazy proxy ahead of the theme bundle;
Material still owns initialization and rendering, while the pinned runtime is
fetched only when a page contains a diagram. Diagrams therefore keep the
site's fonts and light/dark palettes and work with instant navigation without
charging every documentation page for the runtime. To upgrade, update the
version and both digests in `tools/fetch_mermaid.py` (Mermaid releases:
https://github.com/mermaid-js/mermaid/releases); `tools/check_mermaid.py`
revalidates every fence against the new runtime in CI.

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

Emscripten builds only the interactive documentation examples; it is not a
library dependency. CI pulls the upstream project's multi-platform image by
immutable manifest digest rather than executing a third-party setup action.
The demos are single-threaded, use no filesystem, and compile the same
pathfinding headers as the native self-checking model.

## Emdawnwebgpu

- Emdawnwebgpu port version: `v20260423.175430`
- Dawn revision: `01940842b667a7812d0e4ca0ef4367fbec294241`
- Port SHA-512:
  `42784f70b67197c614322f9fabb0f1dc64228a0de10e88f99941fa9d29bee9ad6683f4651d4eefd5a7a9fbd1f976eb522b190b683219ed1793e9b531c602ffa6`
- Emscripten WebGPU documentation:
  https://emscripten.org/docs/porting/multimedia_and_graphics/WebGPU-support.html
- Emdawnwebgpu package documentation:
  https://dawn.googlesource.com/dawn/+/refs/heads/main/src/emdawnwebgpu/pkg/README.md
- WebGPU specification: https://gpuweb.github.io/gpuweb/
- Stable C header project: https://github.com/webgpu-native/webgpu-headers

The optional browser compute example uses Emscripten's exact
`--use-port=emdawnwebgpu` package and `--closure=1`. Its version, Dawn commit,
and archive digest above are the metadata shipped by Emscripten 6.0.3. The
public backend consumes only the stable WebGPU C API and is compiled only when
`TESS_ENABLE_WEBGPU` is defined. Consumers supply their own header and device;
normal CPU-only builds neither fetch nor link Emdawnwebgpu.

## Dear ImGui

- Current verified release: `v1.92.8` (`8936b58fe26e8c3da834b8f60b06511d537b4c63`,
  published 2026-05-12)
- Documentation: https://github.com/ocornut/imgui (README, `docs/`, wiki)
- Repository and releases: https://github.com/ocornut/imgui

Optional, consumer-provided integration dependency for the header-only
reference panels and world tools in `include/tess/debug/imgui/`. tess core and
normal builds never fetch, link, or require ImGui: the headers compile only
when the consumer defines `TESS_ENABLE_IMGUI`, and the consumer supplies its
own Dear ImGui and includes `<imgui.h>` first (a `#error` enforces the order).
Only the stable core
`Text`, `TextUnformatted`, `Separator`, and `Checkbox` functions and the
`IMGUI_VERSION` macro are used. No minimum version is imposed; the release
above is the current known-compatible reference. CI validates the headers
against a minimal API-matching stub (`tests/imgui_stub/imgui.h`,
`tess_diagnostics_panels_test`, `tess_imgui_tools_test`) and separately builds
the Pages diagnostics demo from the six required sources at the exact revision
above. The demo publishes upstream `LICENSE.txt` beside its artifacts. This
real-library check is isolated to `tools/build_web_demo.sh`, so tess packages
and ordinary builds add no ImGui dependency.

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
  own EnTT-dependent test, example, and benchmark targets, which acquire real
  EnTT through `tess_require_entt()`. The default dependency mode uses the
  retrying, exact-revision shallow `FetchContent` path at the pinned SHA
  (`SYSTEM`/`EXCLUDE_FROM_ALL`);
  `TESS_USE_SYSTEM_DEPENDENCIES=ON` instead requires EnTT 3.16.0 or newer.
  The feature default stays `OFF` so ordinary consumer builds never fetch.
  A parent-provided `EnTT::EnTT` target takes precedence as a documented trust
  boundary and bypasses tess's package-version validation.

EnTT requires C++17; tess builds it under `cxx_std_20`. The pinned SHA is
exercised by the repository's required platform matrix. The dependency-free
concepts layer
(`include/tess/ecs/adapter.h`) is always built and tested without EnTT.

## Flecs

- Version: `v4.1.5` (SHA-pinned in `cmake/TessFlecsDeps.cmake` to
  `d7d0c4f7afb4518a6bae749efdc52c7cb5cffee6`; latest upstream release as
  of 2026-07-22)
- Documentation: https://www.flecs.dev/flecs/
- Building and CMake target documentation:
  https://www.flecs.dev/flecs/md_docs_2BuildingFlecs.html
- Repository and releases: https://github.com/SanderMertens/flecs

Optional integration dependency for
`include/tess/ecs/flecs/flecs_adapter.h`. The policy mirrors EnTT: tess core
never fetches or links Flecs, the consumer defines `TESS_ENABLE_FLECS` on an
individual target and includes `<flecs.h>` first, and the header is otherwise
inert. The header requires Flecs 4.1.5 or newer through its public
`FLECS_VERSION_*` macros.

`TESS_ENABLE_FLECS` is also an independent CMake option, default `OFF`, that
enables only tess's Flecs test, example, and benchmark targets. Developer,
release, benchmark, and Windows presets turn it on and acquire the exact
commit through the retrying shallow-fetch helper. Consumer and dependency-free
example presets leave it off. `TESS_USE_SYSTEM_DEPENDENCIES=ON` uses the
installed `flecs::flecs_static` target. Upstream's installed
`flecs-config.cmake` has no ConfigVersion companion, so the adapter's
compile-time version check is the system-package compatibility gate. A
parent-provided target remains an explicit trust boundary.

Flecs entity IDs are 64-bit values whose high bits include generation data;
the adapter preserves all bits in `EntityHandle`. Zero is Flecs' invalid ID.
The C++ API requires C++17; tess uses C++20. Its persistent query is created
once in `FlecsPathAgentContext`, because Flecs documents repeated query
creation as expensive. Adapter collection never performs structural mutation;
goal removal and other structural changes occur only after iteration.

## Cloud bare-metal campaign tooling

- Google Cloud CLI (`gcloud`, `gsutil`): https://cloud.google.com/sdk/docs
- GCE bare-metal instances:
  https://cloud.google.com/compute/docs/instances/bare-metal-instances
- `numactl`: https://github.com/numactl/numactl
- `util-linux` (`taskset`, `lscpu`):
  https://github.com/util-linux/util-linux
- Linux CPUFreq governors:
  https://www.kernel.org/doc/html/latest/admin-guide/pm/cpufreq.html
- Linux `perf stat`: https://man7.org/linux/man-pages/man1/perf-stat.1.html
- Linux `perf` event modifiers:
  https://man7.org/linux/man-pages/man1/perf-list.1.html

Operator-side, campaign-only, and required by nothing the library, tests,
benchmarks, or CI build. `tools/cloud/run_metal_bench.sh` and
`tools/cloud/reap_orphans.sh` run on the operator's machine and need only
the Google Cloud CLI; both are written for the bash 3.2 that macOS ships,
because the safety net failing on the host an operator is most likely to
use is the failure that matters.

The remaining tools are installed by `tools/cloud/setup_metal_vm.sh` on
the instance itself: `numactl` for the thread-scaling sweep's memory
policy, `taskset` and `lscpu` (util-linux, present on the Ubuntu image)
for its per-point CPU pinning, and a kernel-matched `linux-tools` package
for `perf`. `linux-tools-generic` does NOT match the GCE kernel and
`linux-tools-common` alone provides only a wrapper that errors, so the
install tries the exact kernel first and then the GCE flavour. A missing
`numactl`, `taskset`, or writable cpufreq governor does not abort the
run; each is counted as a failure so the sweep is recorded as not
publishable rather than silently measured under uncontrolled conditions.
The CSV parser accepts an exact generic event name with or without documented
privilege modifiers, because access restrictions can make `perf` report
`cycles:u` even when the requested event was `cycles`. Unknown suffixes and
prefix collisions remain invalid.

See [planning/cloud-campaign.md](planning/cloud-campaign.md) for the
runbook and the cleanup mechanisms.

## GitHub Actions

- Checkout action version: `actions/checkout@v7.0.1` (pinned to
  `3d3c42e5aac5ba805825da76410c181273ba90b1`)
- Checkout documentation: https://github.com/actions/checkout
- Cache action version: `actions/cache@v6.1.0` (pinned to
  `55cc8345863c7cc4c66a329aec7e433d2d1c52a9`)
- Cache documentation: https://github.com/actions/cache
- Upload artifact action version: `actions/upload-artifact@v7.0.1` (pinned to
  `043fb46d1a93c77aae656e7c1c64a875d1fc6a0a`)
- Upload artifact documentation: https://github.com/actions/upload-artifact
- Download artifact action version: `actions/download-artifact@v8.0.1` (pinned
  to `3e5f45b2cfb9172054b4087a40e8e0b5a5461e7c`)
- Download artifact documentation:
  https://github.com/actions/download-artifact
- Setup Python action version: `actions/setup-python@v7.0.0` (pinned to
  `5fda3b95a4ea91299a34e894583c3862153e4b97`)
- Setup Python documentation: https://github.com/actions/setup-python
- Hosted runner documentation:
  https://docs.github.com/actions/reference/runners/github-hosted-runners
- Hosted runner image inventories and label guidance:
  https://github.com/actions/runner-images
- Windows 2022 image inventory:
  https://github.com/actions/runner-images/blob/main/images/windows/Windows2022-Readme.md
- Job-condition documentation:
  https://docs.github.com/actions/how-tos/write-workflows/choose-when-workflows-run/control-jobs-with-conditions
- Required-check troubleshooting:
  https://docs.github.com/pull-requests/collaborating-with-pull-requests/collaborating-on-repositories-with-code-quality-features/troubleshooting-required-status-checks
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
`windows-2025` for current-toolchain jobs, plus `windows-2022` for the Visual
Studio 2022 release floor — instead of `-latest` labels. This avoids automatic
OS-family migrations, but GitHub refreshes each hosted image in place, so its
compilers, CMake, and preinstalled tools still roll. GitHub currently documents
the public x64 Ubuntu runner as four CPUs with 16 GB of RAM; the clang-tidy cap
matches those CPUs. The standard `macos-15` runner has three M1 CPUs and 7 GB
of RAM, so its uncached release-floor build is capped at three compile jobs.
The blocking Linux analysis jobs install and invoke `clang-tidy-18` explicitly;
the weekly advisory job follows the runner's rolling unversioned `clang-tidy`
package so newer diagnostics surface without changing the required baseline.
Other preinstalled tools still roll with the runner image. Benchmark baseline
JSON is uploaded from CI artifacts so timing thresholds can be calibrated
against the same runner family that will enforce them; benchmark gates
therefore run only on the Linux runner family they were calibrated on. Every
checkout disables persisted Git credentials because these
jobs only need repository read access.

The required CI workflow always runs its change classifier, hook backstop, and
aggregate gate. When a complete NUL-delimited Git diff contains only
`docs/**`, Markdown files, or `mkdocs.yml`, the platform builds, compiled
analysis, and performance gates are conditionally skipped. The classifier
fails closed on empty changes, invalid revisions, and Git errors. This uses
job conditions rather than workflow path filters: GitHub documents that a
skipped required workflow can remain pending, while a conditionally skipped
job reports success. It also avoids the native path filter's 300-file limit.

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
- clang-format Python package version: `22.1.8`
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
- Clang-tidy 18 documentation:
  https://releases.llvm.org/18.1.8/tools/clang/tools/extra/docs/clang-tidy/
- Clang-tidy 22 release notes:
  https://releases.llvm.org/22.1.0/tools/clang/tools/extra/docs/ReleaseNotes.html

Used by the opt-in `dev-clang-tidy` preset through the `CXX_CLANG_TIDY` target
property. The required preset analyzes local example and test targets;
benchmarks are built in their separate performance job without clang-tidy.
Third-party targets are not linted by project policy. Required CI caps the
analysis build at four concurrent jobs; an explicit `--parallel 4` is portable
across CMake generators and avoids unbounded runner memory pressure.

## clangd

- Configuration documentation: https://clangd.llvm.org/config
- Feature documentation: https://clangd.llvm.org/features#clang-tidy-checks

Used for editor diagnostics and navigation. Start clangd with
`--clang-tidy --enable-config`; the checked-in `.clangd` points clangd at the
default developer compilation database in `build/dev`, and `.clang-tidy`
selects the clang-tidy checks.

The `dev-clang-tidy` preset is a CI quality gate for low-noise
clang-analyzer, bugprone, performance, and selected readability checks. The
blocking Linux jobs use the explicit `clang-tidy-18` executable for a stable
diagnostic baseline. The source remains clean under locally verified
clang-tidy 22.1.8; that release expanded `bugprone-exception-escape`, which
requires narrowly documented suppressions where vector capacity is proven
before a conditionally `noexcept` concurrent callback.

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
property. Tess sets the property only on the local `tess_smoke` target, whose
umbrella includes cover the public product headers without analyzing
third-party targets. Cppcheck 2.21.0 crashes in its template simplifier on
several valid, template-heavy test instantiations; the compiler, clang-tidy,
warnings-as-errors, and sanitizer gates retain per-instantiation coverage. The
preset must be retried without the `tess_smoke` target restriction whenever
the supported cppcheck version changes. Retain whole-target analysis when the
template-heavy test and benchmark translation units complete without an
internal error. The preset enables `warning` and `portability` checks;
cppcheck `style` and
`performance` checks are intentionally deferred because early runs mostly
report low-signal advice for small value types and static member functions in
this template-heavy API. It narrowly suppresses cppcheck `internalError` for
`include/tess/core/shape.h`, where cppcheck fails while analyzing `ShapeTraits`
non-type template parameter constants. The queued-operation planner uses an
inline `returnDanglingLifetime` suppression where cppcheck reports a false
positive for a pointer to an element inside a caller-provided span.

## LLVM libc++

- Documentation: https://libcxx.llvm.org/
- Ubuntu packages: `libc++-dev`, `libc++abi-dev`

Used by the pull-request libc++ portability cell, which configures the
`dev-werror` preset with `CXXFLAGS=-stdlib=libc++` and builds without running
tests. It is a compile-only check: the library is header-only, so what this
guards is that the headers compile against a second standard library
implementation, not that behavior differs. macOS also builds against libc++,
but those jobs are main-only, so before this cell a libc++-specific failure
could reach main before anyone saw it. The packages are installed from the
runner's apt repositories rather than pinned, matching how the runner's Clang
and GCC toolchains are already treated.

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

## vcpkg port helpers

- End-to-end overlay reference: vcpkg registry release `2026.05.25`
  (`d015e31e90838a4c9dfa3eed45979bc70d9357fc`) with vcpkg tool
  `2026-04-08-e0612b42ce44e55a0e630f2ee9d3c533a63d8bc1`.

`ports/tess/vcpkg.json` declares host dependencies on `vcpkg-cmake` and
`vcpkg-cmake-config`. Both are vcpkg's own port-authoring helpers, not
runtime or link dependencies of tess:

- `vcpkg-cmake` provides `vcpkg_cmake_configure` and `vcpkg_cmake_install`,
  which the portfile uses instead of hand-rolled CMake invocations.
- `vcpkg-cmake-config` provides `vcpkg_cmake_config_fixup`, which relocates
  the installed CMake package files into vcpkg's expected layout.

The checked-in port is a filesystem overlay used directly from a tess
checkout. Its `SOURCE_PATH` resolves the repository root relative to
`CMAKE_CURRENT_LIST_DIR`; it does not download or hash the release archive.
A checkout's source files are outside the port directory and therefore do not
participate in vcpkg's ABI hash. The supported installation command disables
binary caching with `--binarysource=clear` so a prior package cannot mask local
source changes. This sacrifices cache reuse only for that invocation; a future
registry port's source checksum will make normal binary caching safe again.
A future central-registry port should instead acquire the published tag with
the registry recipe's independently stored checksum.

- Overlay-port documentation:
  https://learn.microsoft.com/vcpkg/concepts/overlay-ports
- Binary-caching and ABI-hash documentation:
  https://learn.microsoft.com/vcpkg/reference/binarycaching
- `vcpkg_cmake_configure` documentation:
  https://learn.microsoft.com/vcpkg/maintainers/functions/vcpkg_cmake_configure

They are resolved by vcpkg itself when the overlay port is built, are never
fetched by this repository's own build, and reach no consumer of
`tess::tess`. A consumer installing tess through CMake or FetchContent never
sees them.

## Documentation assets (vendored)

- `doxygen-awesome-css` `v2.4.2` — MIT-licensed stylesheet set for the
  Doxygen API reference. Vendored with checksums in
  [`docs/doxygen-awesome/`](doxygen-awesome/README.md).
  Documentation: https://jothepro.github.io/doxygen-awesome-css/
- Fraunces (variable, latin subset) — OFL-licensed heading font for the
  MkDocs site, vendored in [`docs/assets/fonts/`](assets/fonts/README.md).
  Upstream: https://github.com/undercasetype/Fraunces

## Claude Code (agent tooling)

- Documentation: https://code.claude.com/docs/en/memory
- Declared for the `CLAUDE.md` shim files: Claude Code loads only
  `CLAUDE.md` (root plus nested, on demand), never `AGENTS.md`, so each
  shim holds a one-line `@AGENTS.md` import. Other agent CLIs read
  `AGENTS.md` directly and ignore the shims. No build or runtime
  dependency; removing the shims only stops Claude Code sessions from
  loading the agent instructions.
