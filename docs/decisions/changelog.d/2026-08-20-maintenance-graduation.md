## 2026-08-20 - Graduate maintenance with immediate execution

- Graduated the documented task, budget, metrics, opaque handle, explicit
  result, structural backend, registered scheduler, immediate execution, and
  external dense-and-sparse chunk adapter spellings under
  `tess::maintenance`. The stable adapter defaults to immediate execution.
- Kept FIFO, queued-coalescing, dirty-bit, and the virtual scheduler under
  `tess::experimental`. M3 was flat without a material regression, but the
  Steam Deck dirty-bit result materially regressed the immediate guardrail in
  budgeted, flush, and 256- and 1,024-task scaling workloads; the 4,096-task
  cell was inconclusive. The portable decision rule therefore rejects
  dirty-bit graduation even though its correctness gates passed.
- Made graduation a source-level facade over the exact measured task,
  scheduler, and adapter types. No implementation or adapter body, MNT-3
  campaign configuration, build flag, benchmark, or fixture changed, and the
  stable default names the same immediate specialization measured by the
  campaign. The generic paired-sentinel source-map update is CI metadata, not
  an MNT-3 input. The retained M3 and Steam Deck evidence therefore remains
  representative.
- Preserved the compile-time structural customization boundary and callback
  exception semantics. The stable contract does not include a virtual ABI,
  object layout, mixed Tess versions, cross-DSO identity, or experimental
  backend behavior.
- This supersedes the historical maintenance TDD's expectation that a
  coalescing backend would graduate with the public contract. Portable
  evidence selects synchronous immediate execution while leaving deferred
  backend work open to later experimentation.
- This also supersedes the v1-stabilization TDD's requirement that a stable
  aggregate never transitively include an experimental header. The alias-only
  facade must see the measured implementation declarations; it therefore
  makes experimental maintenance spellings reachable through `tess/tess.h`,
  but the support contract grants stability only to the documented
  `tess::maintenance` names and semantics.
