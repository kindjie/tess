# Style

C++ code follows the
[Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html).

## Local Policy

- Format C++ with `clang-format` using the repository `.clang-format`.
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
