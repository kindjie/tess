# tess_queued_results_test

- `tess_queued_results_test`: verifies the S6 result-channel core: a
  default `OpCompletion` is never `ok()` (the `completed` flag gates the
  success triple), `record_plan_completions` copies plan-time rejections
  into `Failed` slots that deliver reasons -- never values -- through
  `drain_results`, drain visits handle (== enqueue) order exactly once
  while `state()`/`completion()` lookups stay readable, `clear()` drops
  slots and bumps the generation, and warm reuse within reserved capacity
  is allocation-free. The execute-wrapper coverage (S6.3): delivery is
  identical under serial and threaded executors for a successful phase
  (handle order, completions, accumulated values, and world fields), a
  runtime PolicyMismatch delivers its reason while the serial early-stop
  tail stays `Pending` (threaded executors complete the whole range by
  contract), a phase issued for another plan fails before touching the
  channel, the serial whole-plan wrapper prepares every op upfront so an
  aborted tail reads `Pending`, and warm result-bearing execution plus
  drain plus clear is allocation-free. A throwing drain visitor leaves that
  slot undrained so a later call can retry it exactly once, including after
  reentrant capacity growth; a reentrant clear retires the old slot without a
  stale recovery write. Cooperative async coverage verifies generation-stamped
  `AsyncTicket`s, immediate results, deterministic FIFO advancement under a
  shared item budget, pending continuations across calls, required/result
  versions, terminal failed/cancelled/superseded/stale states, stale-ticket
  rejection after clear, rejection of callback-time queue mutation,
  throw-and-retry value/state semantics, invalid budget-report accounting,
  callback-invocation bounds for zero-progress work, and allocation-free warm
  submit/advance/reset reuse.
