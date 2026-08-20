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
- Kept current-product freshness tied to the shared content version. A retry
  rebuilds version drift even when this adapter's owned dirty observation is
  empty, while generation-safe clear still cannot remove a disjoint owner's
  bits.
- Required a nonzero, disjoint dirty-mask owner and recorded dirty state before
  offering work. A fixed per-slot retry-debt bit also retains a follow-up that
  cannot enter a bounded comparison queue. Unbounded flush reoffers the debt;
  budgeted drains expose it without risking synchronous work outside their
  budget, and neither can report `Idle` while it remains. This keeps queue
  capacity failure recoverable without making coalesced scheduling an
  exact-event mechanism.
- Kept the complete surface experimental. Cross-platform promotion evidence,
  rather than this correctness integration alone, decides which backend and
  adapter pieces graduate.
