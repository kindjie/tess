## 2026-08-19 - Keep chunk maintenance external and residency-aware

- Added one experimental `ChunkMaintenanceAdapter` as the first real consumer
  of the registered scheduler contract. The adapter borrows an immovable world
  and owns its scheduler, fixed tasks, handles, and derived product slots;
  authoritative storage and world construction remain unchanged.
- Bound dense slots directly to chunk keys and sparse slots to resident
  capacity using `{key, residency_generation}`. Sparse rebinding is allowed
  only after producers close and join and an explicit drain returns a fresh
  positive `Idle`; arbitrary direct world residency mutation is unsupported.
- Made each product's key, content version, and residency generation explicit.
  A task rechecks sparse residency before unchecked access, publishes only
  after its callback returns, and clears only its generation-safe dirty
  observation. Intervening marks, eviction/reload, archive load, and exceptions
  therefore leave products stale or unavailable and authoritative work
  retryable.
- Required a nonzero, disjoint dirty-mask owner and recorded dirty state before
  offering work. This keeps queue capacity failure recoverable without making
  coalesced scheduling an exact-event mechanism.
- Kept the complete surface experimental. Cross-platform promotion evidence,
  rather than this correctness integration alone, decides which backend and
  adapter pieces graduate.
