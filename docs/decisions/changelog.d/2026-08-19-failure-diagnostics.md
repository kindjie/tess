## 2026-08-19 - Match diagnostics to the recoverability boundary

- Expected domain outcomes continue through statuses and checked lookups;
  unchecked coordinate and span hot paths retain their documented debug-only
  assertions.
- Object-lifecycle and ownership violations fail fast in every build when
  continuing would fabricate a normal-looking result, orphan retained
  accounting, or mutate storage during its own dispatch.
- Path-result publication is transactional. A processing pass publishes only
  after every borrowed path span is installed, and interrupted passes expose no
  partial batch through `results()` or `try_result()`.
- `PathTicket` remains an additive two-field value. Stale, out-of-range, and
  unpublished states are detectable; foreign-runtime provenance remains a
  documented precondition because matching index and generation values cannot
  encode ownership.
- Compile-fail fixtures protect stable Tess-authored requirement phrases while
  leaving compiler-specific framing and instantiation traces unconstrained.
