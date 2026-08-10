# tess_sim_schedule_test

- `tess_sim_schedule_test`: verifies the S7 Schedule core: phase-major then
  registration-order execution, `every_ticks(n)` exactness with disablement
  keeping lockstep (the countdown advances while disabled; the due tick is
  counted as skipped), OnDirty firing iff the task's own mask bits are
  pending with own-bit-only consumption (foreign bits sit inert), produced
  dirty reaching later phases the same tick and earlier phases the next
  tick, OnEvent mask coalescing with own-bit-only consumption, produced events
  reaching later phases the same tick and earlier phases the next tick, exact
  ordered `EventStream` entries carrying tick and sequence stamps with bounded
  overflow, deterministic background item budgets with `more_work`
  continuation, manual single-shot runs, persistent triggers surviving
  disablement, allocation-free dispatch (`run_tick`/`notify_dirty`/
  `notify_events`/`request_run`) after `seal()`, allocation-free warm event
  publication, direct `ResumableWorkTask` background integration, and the
  explicit re-arm required after that task quiesces. Throwing callbacks restore
  consumed dirty, event, and manual triggers while preserving clock/cadence
  advancement, including EveryN phase alignment for tasks ordered after the
  throwing callback. The frame driver keeps EveryN exact
  across SimSpeed changes, backlogged multi-tick frames, and paused frames
  (cadences count fixed ticks, never frames).
  Explicitly `noexcept` task objects are stored through the no-throw erased
  signature and run successfully.
