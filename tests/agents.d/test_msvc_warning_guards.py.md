# test_msvc_warning_guards.py

- Pins the compile-time branch forms required by the supported MSVC 19.44
  warning floor. This is intentionally a source-level test: newer MSVC, Clang,
  and GCC do not reproduce the floor compiler's C4127 warning, so ordinary
  local compilation would pass vacuously.
