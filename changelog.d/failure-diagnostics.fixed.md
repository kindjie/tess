- Runtime contract violations that previously became plausible idle or
  `NoPath` results in assertion-disabled builds now fail fast with API-specific
  diagnostics. `PathRequestRuntime::try_result` provides checked ticket lookup
  against transactionally published batches; schedule lifecycle and result
  accounting, resumable-queue reentrancy, and flow-owner rebinding are enforced
  consistently in debug and release builds. Representative public template
  failures now explain the required type or value.
