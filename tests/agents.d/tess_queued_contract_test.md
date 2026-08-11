# tess_queued_contract_test

- `tess_queued_contract_test`: pins queued-operation boundary types, lifecycle,
  hazards, phase execution, dirty ordering, executor adaptation, and warm
  allocation behavior. Explicitly `noexcept` callbacks must remain no-throw
  through partitioned and result-channel adapters; typed batch payloads remain
  non-owning while preserving planner metadata.
- The payload-view test distinguishes three states that `as<T>()` used to
  answer identically with an empty span: a wrong-type query, an operation
  carrying no payload, and a correctly-typed empty batch. Asserting a
  wrong-type read returns size zero pins nothing — a correct empty batch
  returns that too — so the type question is asked through `holds<T>()`.
  The abort path lives in `tess_assert_test`.
