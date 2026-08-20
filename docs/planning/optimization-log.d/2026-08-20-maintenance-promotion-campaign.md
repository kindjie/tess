## 2026-08-20 - External maintenance adapter promotion campaign

- **Hypothesis:** the registered dirty-bit backend will materially beat the
  queued-coalescing primary control on at least one of Apple M3 and Steam Deck
  without a material primary or immediate-execution guardrail regression on
  either device.
- **Candidate and method:** source
  `b4a882bbdaa32a704109d5bdd773a1adfe45b492`, built separately with the
  recorded native Apple toolchain and pinned Steam Runtime image. Each device
  ran a separate 30-block queued-coalescing A/A calibration, then 30 paired,
  SHA-ranked blocks across dense, sparse, mixed, flush, budgeted, and
  16/64/256/1,024/4,096 registered-task cells. Every cell compared dirty bit
  with immediate, FIFO, and queued-coalescing backends. CPU time was the
  decision metric; absolute times were never compared across devices.
- **Correctness gate:** the exact source completed the normal suite with no
  failures (1,567 passed and one intentionally unsupported capability
  skipped). Adapter-focused ASan/UBSan, TSan, and warnings-as-errors runs each
  passed 21/21 cases, and campaign-tool tests passed 43/43. The adapter cases
  cover deterministic 1,000-run flush, archive-v2 and independent-rescan
  equivalence, dirty ownership, typed content/residency generations,
  generation-safe clear, retry, budget, exceptions, shutdown, concurrency,
  and warmed dense/sparse zero-allocation behavior across all backends.
- **Calibration:** every A/A workload was valid. The largest paired relative
  noise p95 was 2.21% on Steam Deck, below the frozen 10% invalidation ceiling.
  Candidate thresholds remained the predeclared maximum of the fixed floors
  and twice each device's measured A/A noise.
- **M3 result:** `flat` overall. It provides neither the material primary win
  required to graduate dirty bit nor a material regression.
- **Steam Deck result:** the aggregate primary comparison against queued
  coalescing is `flat` at +1.46%, with a 95% interval of +1.36% to +1.64%
  against the 8% relative and 4.98 us absolute thresholds. The overall device
  decision is nevertheless `material_regression`: dirty bit is materially
  slower than immediate execution in budgeted (+10.06%), flush (+10.16%),
  scaling-256 (+10.32%), and scaling-1,024 (+10.85%) cells. The scaling-4,096
  immediate interval crosses the regression boundary and is `inconclusive`.
- **Memory limitation:** isolated scaling-4,096 M3 processes recorded one-off
  roughly 1.3 MiB peak-RSS excursions under dirty bit, FIFO, and immediate.
  Exact work counters, non-monotonic repetitions, lower dirty-bit median than
  coalescing, and green sanitizer/allocation gates do not indicate a leak.
  The protocol declared no memory threshold, so this remains descriptive and
  no post-result gate was invented.
- **Decision:** `keep_experimental`. `DirtyBitScheduler` does not satisfy the
  portable performance rule. This performance-only result does not block the
  separately validated stable task, handle, result, adapter, and immediate-
  execution contract from promotion.
- **Evidence and limitation:** the public sanitized bundle is retained under
  [`evidence/v0.13/maintenance/`](../evidence/v0.13/maintenance/), separately
  manifested by its inner `PUBLIC_EVIDENCE_SHA256SUMS`; the raw set stays
  external and unchanged under
  `CROSS_DEVICE_EVIDENCE_V4_SHA256SUMS`, SHA-256
  `407f6279aad3a27442ca4fb8673712baf1b7c4a152c20e74607ff0a10cd77cb0`, with
  the sanitized and omitted members pinned in the directory's redaction map.
  The Deck wrapper's aggregate console transcript/status was not separately
  captured; authoritative calibration and candidate phase statuses,
  inventories, logs, governor evidence, and replay outputs are retained.
- **Reconsideration:** retry only after a relevant implementation, adapter,
  benchmark, fixture, compiler, flag, or SDK change, then refreeze and rerun
  correctness plus both hardware legs. A mechanical move needs an explicit
  representativeness record rather than an assumed carry-forward.
