# Installation and packaging

The library is header-only. A consumer needs a C++20 compiler and CMake 3.25
or newer; tess itself adds no runtime or link dependency.

## Installed CMake package

```sh
cmake --preset consumer
cmake --install build/consumer --prefix "$HOME/.local"
```

Point an application at a non-system prefix during its configure step:

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH="$HOME/.local"
```

Then consume the exported target:

```cmake
find_package(tess 0.3 CONFIG REQUIRED)
target_link_libraries(my_target PRIVATE tess::tess)
```

## FetchContent

```cmake
include(FetchContent)
FetchContent_Declare(
  tess
  GIT_REPOSITORY https://github.com/kindjie/tess.git
  GIT_TAG v0.3.0
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(tess)
target_link_libraries(my_target PRIVATE tess::tess)
```

Pin a release tag or immutable commit. Because tess sees itself as a
subproject, its tests and examples default off and no development dependency
is downloaded.

## Package-manager status

No tess recipe has been accepted into vcpkg or Conan Center yet. Until one is,
the installed-package and `FetchContent` paths above are the supported paths.
An in-repository vcpkg overlay or Conan recipe may be used to prove packaging
before proposing it to a central registry, but central submissions should
follow a public, stable release rather than an unreleased commit.
