# Style

C++ code follows the
[Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html).

## Local Policy

- Format C++ with `clang-format` using the repository `.clang-format`.
- Build local test and benchmark targets with project warning flags by default.
- Use `dev-werror` when checking that project warnings remain clean as errors.
- Use `dev-asan` for AddressSanitizer and UndefinedBehaviorSanitizer test
  runs.
- Use `dev-clang-tidy` for opt-in clang-tidy checks when `clang-tidy` is
  installed.
- Use `dev-cppcheck` for opt-in cppcheck analysis when `cppcheck` is
  installed.
- Run clangd with `--clang-tidy --enable-config` so editor diagnostics use the
  project `.clangd` and `.clang-tidy` files.
- Use `.h` for C++ headers.
- Use `.cc` for C++ implementation, test, and benchmark files.
- Keep lines at 80 columns where practical.
- Follow the public repository safety rules in the root `AGENTS.md`.

## Exception Specifications

Prefer `noexcept` for public library functions, operators, and hot-loop
accessors unless there is a clear semantic need to propagate exceptions. The
current core shape/key helpers and storage page accessors are intended to be
no-throw.

Compile-time validation failures should use `static_assert` rather than runtime
exceptions. Unchecked hot-loop accessors may remain `noexcept` when their
contract requires valid typed tags, keys, coordinates, or local tile ids.

Tests should make this policy verifiable with `static_assert(noexcept(...))`
coverage for public APIs as they are added.

## Public And Implementation Headers

`TESS_PUBLIC_HEADERS` in the root CMake file defines the supported API surface.
Headers in `TESS_IMPLEMENTATION_HEADERS` are installed only because public
templates include them. Names in `tess::detail`, and direct inclusion of those
implementation headers, carry no source-compatibility guarantee. Consumer code
should include the narrowest public header that owns the API it uses; the
`tess/tess.h` umbrella remains available for convenience but has the highest
compile cost.

## API Documentation

Use Doxygen-style comments for public API contracts. Prioritize ownership and
borrowing, invalidation, allocation behavior, thread safety, checked versus
unchecked entry points, and sparse-residency behavior over restating names or
types. `tools/check_public_docs.py` gates namespace-scope symbols in an explicit
first slice of headers; add a header to its `DEFAULT_HEADERS` only after every
namespace-scope public type and free function in that header is documented.
The lightweight checker does not validate members or claim full API coverage.
