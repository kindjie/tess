# test_macro_odr_guards.py

- `tests/test_macro_odr_guards.py`: a source-level test, not a tool test,
  asserting that every build-wide macro switch carries an ODR mismatch
  guard — `TESS_HAS_EXCEPTIONS`, `TESS_INTERNAL_CAPACITY_TESTING`,
  `TESS_ENABLE_DIAGNOSTICS`, and `TESS_ENABLE_ASSERTS`, each paired with
  the header that decides its behaviour and its `detect_mismatch` key. It
  pins that every macro has a pragma at all, that each key is stamped in
  both the enabled and the disabled branch (a pragma on one branch only
  is worse than none: the mismatch it exists to catch is precisely one
  translation unit defining the macro and another not, and an unstamped
  branch contributes no symbol to disagree with), and that every pragma
  sits inside a `#if defined(_MSC_VER)` block, since GCC and Clang can
  reject unknown pragmas under `-Werror`. Scanning the source is
  deliberate: `#pragma detect_mismatch` is MSVC-only, so a
  compile-and-link check would pass vacuously on the platforms CI mostly
  runs on.
