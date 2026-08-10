# Tests

- C++ test targets use GoogleTest. The `test_*.py` files are pytest suites
  for the repository tooling; they are not registered with CTest (the CI
  hook-backstop job runs them, and pins its file list to the glob).
- Per-test documentation lives in `tests/agents.d/<name>.md`, one fragment
  per GoogleTest target and per pytest file. Before modifying or adding a
  test, read (or write) its fragment: fragments record what a test pins,
  why specific constants were chosen, and which wider claims are
  deliberately not made. CI enforces exact synchronization
  (`tools/git_hooks.py ci`): a target or `tests/test_*.py` file without a
  fragment fails, and so does an orphan fragment whose test no longer
  exists, an empty fragment, or a first line that is not `# <name>`.
- Shared A*-defeating serpentine-maze fixtures and reference oracles live
  in `tests/path_test_util.h`; read the `tess_path_search_test` and
  `tess_diagnostics_enabled_test` fragments before changing them (the
  mazes carry permanent heap-mutation guards).
- Benchmark-binary conventions live in `bench/AGENTS.md`.
- `allocation_counter.{h,cc}` is shared only by allocation-sensitive test
  binaries that need global `new`/`delete` counters. Do not link it into more
  than one translation unit inside the same executable.
- Allocation counting is enabled exclusively through the RAII
  `tess_test::ScopedAllocationCounter` (construction resets and enables,
  destruction disables). There is intentionally no free-function
  enable/disable API: failed `ASSERT_*`s return early, and a raw flag would
  leak enabled counting into later tests. Read results via `counter.count()`
  / `counter.bytes()` inside the scope, or the read-only
  `tess_test::allocation_count()` / `allocation_bytes()` snapshots. The
  counter state is relaxed-atomic: it can under-count cross-thread
  allocations racing a scope boundary (never over-counts, so `== 0`
  assertions never false-fail).
- `tess_test::ScopedAllocationFailure` rejects one zero-based allocation
  attempt and reports the attempted-allocation count. Use it to prove strong
  allocation-failure guarantees by retrying successive ordinals until the
  operation succeeds. Unsupported guards are inert. Rejection is unavailable
  under AddressSanitizer and ThreadSanitizer because their allocation hooks
  observe but cannot reject. It is also unavailable with MSVC checked
  iterators: their vector constructors and moves allocate proxy state inside
  `noexcept` functions, so rejecting that bookkeeping would terminate the
  process. Tests using injection must skip when the capability reports false;
  Windows CI runs the strong-guarantee cases in Release as well as retaining
  checked-iterator coverage in Debug.
- Under AddressSanitizer or ThreadSanitizer, `allocation_counter.cc` and
  `bench/tess_diagnostics_alloc_hooks.cc` must use sanitizer allocation
  hooks instead of replacing global `new`, so the sanitizer's alloc/dealloc
  mismatch and alloc-dealloc-size checks remain meaningful. Without a
  sanitizer they replace the complete standard `operator new`/`delete`
  overload set (plain, array, aligned, nothrow) so every allocation form is
  counted portably (no dlsym chaining to Itanium-mangled symbols).
