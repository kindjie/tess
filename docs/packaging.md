# Installation

The library is header-only. A consumer needs a C++20 compiler and CMake 3.25
or newer; tess itself adds no runtime or link dependency. Installing it needs
no network access and builds no code. GoogleTest, Google Benchmark, and EnTT
are development or optional integration dependencies fetched only by
developer presets; ordinary consumers do not link them through `tess::tess`.

## FetchContent

Pin a release tag or immutable commit rather than a moving branch:

```cmake
include(FetchContent)
FetchContent_Declare(
  tess
  GIT_REPOSITORY https://github.com/kindjie/tess.git
  GIT_TAG v0.4.0
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
find_package(tess 0.4 CONFIG REQUIRED)
target_link_libraries(my_target PRIVATE tess::tess)
```

## Choose an include surface

The public CMake target is `tess::tess`. For a focused include surface, use
`<tess/pathfinding.h>` for worlds and routing, `<tess/simulation.h>` for the
full simulation stack, or `<tess/tess.h>` for the all-in-one compatibility
umbrella. All three are dependency-free. The EnTT adapter and Dear ImGui
panels are opt-in headers that consumers include after their corresponding
third-party header; see [ECS integration](architecture/ecs.md) and
[Diagnostics](architecture/diagnostics.md). In compile-sensitive code,
prefer the narrowest public header that owns the API.

## Package-manager status

No tess recipe has been accepted into vcpkg or Conan Center yet. Until one is,
the installed-package and `FetchContent` paths above are the supported paths.
An in-repository vcpkg overlay or Conan recipe may be used to prove packaging
before proposing it to a central registry, but central submissions should
follow a public, stable release rather than an unreleased commit.
