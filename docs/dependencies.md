# Dependencies

## GoogleTest

- Version: `v1.17.0`
- Documentation: https://google.github.io/googletest/
- CMake quickstart:
  https://google.github.io/googletest/quickstart-cmake.html
- Release: https://github.com/google/googletest/releases/tag/v1.17.0

Used for C++ unit tests.

## Google Benchmark

- Version: `v1.9.5`
- Documentation: https://google.github.io/benchmark/
- Repository and releases: https://github.com/google/benchmark

Used for opt-in C++ benchmarks.

On macOS, benchmark configure or execution may warn that pthread affinity or CPU
frequency metadata is unavailable. Those warnings do not prevent benchmark
measurements from running.

## GitHub Actions

- Checkout action version: `actions/checkout@v6`
- Checkout documentation: https://github.com/actions/checkout
- Upload artifact action version: `actions/upload-artifact@v7`
- Upload artifact documentation: https://github.com/actions/upload-artifact
- Hosted runner documentation:
  https://docs.github.com/actions/reference/runners/github-hosted-runners

CI pins `ubuntu-24.04` instead of `ubuntu-latest` to avoid runner image drift.
Benchmark baseline JSON is uploaded from CI artifacts so timing thresholds can
be calibrated against the same runner family that will enforce them.

## tiktoken

- Version: `0.13.0`
- Documentation and releases: https://pypi.org/project/tiktoken/
- Repository: https://github.com/openai/tiktoken

Used by the Git pre-commit hook to count tokens in staged text files through
the Python API. The hook reads staged blobs from Git, so the library API is a
better fit than a filesystem-oriented command-line wrapper.

## CMake clang-tidy Integration

- Documentation:
  https://cmake.org/cmake/help/latest/variable/CMAKE_LANG_CLANG_TIDY.html
- Target property:
  https://cmake.org/cmake/help/latest/prop_tgt/LANG_CLANG_TIDY.html

Used by the opt-in `dev-clang-tidy` preset through the `CXX_CLANG_TIDY` target
property. Tess sets the property only on local test and benchmark targets so
third-party targets are not linted by project policy.

## clangd

- Configuration documentation: https://clangd.llvm.org/config
- Feature documentation: https://clangd.llvm.org/features#clang-tidy-checks

Used for editor diagnostics and navigation. Start clangd with
`--clang-tidy --enable-config`; the checked-in `.clangd` points clangd at the
default developer compilation database in `build/dev`, and `.clang-tidy`
selects the clang-tidy checks.

## Clang Sanitizers

- AddressSanitizer documentation:
  https://clang.llvm.org/docs/AddressSanitizer.html
- UndefinedBehaviorSanitizer documentation:
  https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html

Used by the opt-in `dev-asan` preset for tests. Tess applies sanitizer compile
and link flags to local executables only, because AddressSanitizer must be
linked into the final executable.

## Deferred Executor Candidates

No block-executor dependency is added yet. `work_contract`, Taskflow, oneTBB,
and enkiTS remain candidates for later executor backends after Tess has an
internal executor abstraction and benchmark comparisons.
