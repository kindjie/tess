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
  two shipped movement-commit tiers that resolve contention. Both are
  documented in the simulation architecture note, but neither appeared in
  the pathfinding guide nor in the roadmap's shipped list, which
  `guide/README.md` designates authoritative; both now name them, with the
  swap policy stated rather than assumed. And `ScheduleTaskDesc` was
  described as a phase and cadence, omitting the required static-storage
  name.
- Documentation tooling statements now match the tooling. The style guide
  told contributors to add headers to an opt-in `DEFAULT_HEADERS` list that
  has been derived from the CMake header sets since `ee76d98` — an
  instruction that could not be followed, describing the gate as opt-in
  when it covers every installed header. It also now states the gate's real
  floor: it does not validate members, and it accepts an undocumented
  declaration when another with the same normalized signature is
  documented. `docs/llms.txt` gained the integration-policy page, the one
  that answers the exceptions, RTTI, determinism, and allocation questions
  an integrator asks first and the only nav-level page it omitted. And the
  concepts index no longer carries a second, unenforced copy of the TDD
  index, which had already drifted one entry behind the real one.
- The example smoke gate in CI asserted that at least 13 examples ran while
  the `dev` preset builds 14, under a comment claiming every example runs.
  An example could stop building and the gate would still pass. It is now
  an exact count.
- Internal milestone labels (`M5`, `M10`, `M11`, `S5.3`…) no longer appear
  in the Concepts pages, which `for-agents.md` designates normative. They
  were planning vocabulary that resolved to nothing a reader could look up.
