- Four documentation statements that contradicted the code or another
  maintained page are corrected. The getting-started tutorial said the
  parallel executors are prototypes and that every published performance
  median is single-threaded; the worker pool is the production parallel
  backend and `performance.md` publishes four-worker figures, so a reader
  following the concept ladder was architecting around a serial-only
  assumption. The path architecture note said `store_checked` reports
  pre-allocation capacity failure, but it captures the candidate entry's
  dependencies — which allocates — before the status comes back, as the
  exception-free note already recorded. The pathfinding guide said all
  shipped routing "will not spread or queue a crowd" without mentioning the
  two shipped movement-commit tiers that resolve contention, which are in
  the public umbrella header but appeared on no reader-facing page; the
  roadmap's shipped list now names them. And `ScheduleTaskDesc` was
  described as a phase and cadence, omitting the required static-storage
  name.
