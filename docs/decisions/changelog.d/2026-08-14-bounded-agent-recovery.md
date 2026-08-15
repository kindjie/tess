## 2026-08-14 - Separate liveness budgets from reachability verdicts

- Retry exhaustion is a liveness-policy event, not evidence of `NoPath`.
  `PathAgentTickOptions` therefore defaults to `RemainBlocked`; callers that
  require the historical timeout-as-terminal behavior opt into
  `MarkUnreachable`.
- Expensive blocked checks use caller-owned deterministic exponential backoff
  with equal jitter and a per-tick cap. Scheduling selects work but never owns
  movement-class, sparse-residency, or reachability semantics.
- Legitimate all-agent invalidations use a separate exact FIFO replan queue.
  Its request budget bounds synchronous query count while preserving direct A*
  results and retained-route storage. Existing tick drivers remain synchronous
  and unchanged unless callers opt into the queue.
- Queue and recovery scratch are index-paired, externally synchronized state.
  Independent owners may run concurrently with independent search scratch;
  neither mechanism owns threads or global randomness.
