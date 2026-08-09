- `TESS_ENABLE_ASSERTS` and `TESS_ENABLE_DIAGNOSTICS` now carry
  `#pragma detect_mismatch` on MSVC, matching what `core/config.h` already
  did for the exception mode and `core/capacity.h` for the internal
  capacity hook. Both change the program's shape across translation units
  — the first rewrites 14 inline function bodies in `storage/world.h`
  alone, the second makes `PathCounters`, `TraceBuffer`, `WarningSink` and
  six more types exist or not — so defining either inconsistently violates
  the one-definition rule with no diagnostic on any compiler. These are
  the two macros consumers actually set, and `integration-policy.md`
  actively tells them to set the first, so they were the two most worth
  guarding and the two that were not.
- `integration-policy.md` states the obligation and what the guard does
  and does not cover: MSVC gets a link error, GCC and Clang have no
  equivalent mechanism and consistency there is the build system's job.
- A new `test_macro_odr_guards.py` asserts every build-wide macro switch
  has a guard, that each stamps both states — a pragma on only one branch
  cannot catch the mismatch it exists for, since the unstamped branch
  contributes no symbol to disagree with — and that each sits inside a
  `_MSC_VER` block. The test reads source rather than compiling, because
  the pragma is MSVC-only and a link check would pass vacuously on every
  other toolchain.
