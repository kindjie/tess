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
