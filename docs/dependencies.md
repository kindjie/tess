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

The `dev-clang-tidy` preset is a CI quality gate for low-noise
clang-analyzer, bugprone, performance, and selected readability checks. The
`dev-clang-tidy-advisory` preset uses `.clang-tidy-advisory` for broader noisy
checks that should be reviewed but are not yet part of the blocking gate.
Current advisory findings include known style debt such as redundant
`typename` and swappable coordinate parameters. Promote advisory checks only
after those findings are either fixed or intentionally suppressed.

## Cppcheck

- Version: `2.20.0`
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
`include/tess/core/shape.h`, where cppcheck 2.20.0 fails while analyzing
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
