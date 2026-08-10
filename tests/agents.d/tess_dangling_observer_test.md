# tess_dangling_observer_test

- `tess_dangling_observer_test`: pins the lvalue-only observers on
  `OwnedChunkDomain`, `ExecutionReport` and `ExecutionPlan`. Detection
  traits assert the rvalue forms do NOT compile, because a runtime test
  cannot express that and the deleted overloads would otherwise be
  silently removable; value-returning observers (`size`, `empty`) are
  asserted to stay callable on a temporary, so the fix cannot
  over-restrict. Also pins that `explicit_chunk_domain` sorts without
  deduplicating.
