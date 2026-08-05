# Installation

The library is header-only. A consumer needs a C++20 compiler and CMake 3.25
or newer; tess itself adds no runtime or link dependency. Installing it needs
no network access and builds no code. GoogleTest, Google Benchmark, EnTT, and
Flecs are development or optional integration dependencies fetched only by
developer presets; ordinary consumers do not link them through `tess::tess`.

What tess guarantees once integrated — exceptions, RTTI, determinism
across thread counts, thread ownership, and steady-state allocations —
is in the [integration policy](integration-policy.md).

## FetchContent

Pin a release tag or immutable commit rather than a moving branch:

```cmake
include(FetchContent)
FetchContent_Declare(
  tess
  GIT_REPOSITORY https://github.com/kindjie/tess.git
  GIT_TAG v0.12.0
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(tess)
target_link_libraries(my_target PRIVATE tess::tess)
```

Because tess sees itself as a subproject, its tests and examples default off
and no development dependency is downloaded. The same defaults apply when
vendoring the source with `add_subdirectory`.

## Installed CMake package

The `consumer` preset configures a headers-only install: no tests, examples,
benchmarks, warnings-as-errors, or network fetches.

```sh
cmake --preset consumer
cmake --install build/consumer --prefix "$HOME/.local"
```

Equivalently, without presets:
`cmake -B build -DTESS_BUILD_TESTING=OFF -DTESS_BUILD_EXAMPLES=OFF` followed
by `cmake --install build --prefix ...`.

Point an application at a non-system prefix during its configure step:

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH="$HOME/.local"
```

Then consume the exported target:

```cmake
find_package(tess 0.12 CONFIG REQUIRED)
target_link_libraries(my_target PRIVATE tess::tess)
```

## Choose an include surface

The public CMake target is `tess::tess`. For a focused include surface, use
`<tess/pathfinding.h>` for worlds and routing, `<tess/simulation.h>` for the
full simulation stack, or `<tess/tess.h>` for the all-in-one compatibility
umbrella. All three are dependency-free. The independently gated EnTT and
Flecs adapters and the Dear ImGui panels are opt-in headers that consumers
include after their corresponding third-party header; see
[ECS integration](architecture/ecs.md) and
[Diagnostics](architecture/diagnostics.md). In compile-sensitive code, prefer
the narrowest public header that owns the API.

## Package-manager status

No tess recipe has been accepted into vcpkg or Conan Center yet, so the
installed-package and `FetchContent` paths above remain the supported ones.

The repository does carry both recipes, so the packaging shape can be proven
before anything is proposed to a central registry:

- `conanfile.py` — a Conan 2 recipe. tess is declared a `header-library`
  whose package id clears settings, so one package serves every compiler and
  build type.
- `ports/tess/` — a checkout-based vcpkg overlay port. Use it with
  `--overlay-ports=ports` from the repository root; it packages that checkout
  directly, including local commits, without downloading a release archive.

Both configure the same option set as the `consumer` preset — no tests,
examples, benchmarks, docs, or optional adapters — so what they install is the
surface `find_package(tess)` installs rather than a second definition free to
drift. `tests/test_packaging_recipes.py` pins that correspondence, including
the version, which lives only in `cmake/tess-version.cmake`.

Neither recipe is exercised end to end in CI, since neither tool is installed
there — the tests check the recipes' contents and correspondence, not a
completed package build. A future central-registry vcpkg recipe should fetch
and hash this public stable release instead of using the checkout overlay's
local source path.
