- The budgeted-progress mixed-colony benchmark seats large populations:
  agent placement row stride is now configurable in the colony harness
  (`ColonyConfig::placement_stride`, default unchanged) and the mixed
  matrix places at stride 2, lifting the previous 227-agent seating
  ceiling that aborted population rungs above it. The binary also gains
  `--mixed-only` for resuming campaigns, seats the largest requested
  population up front before any cell runs, reports seated/requested counts
  when placement falls short, and line-buffers stdout so redirected
  campaign logs keep fatal stderr diagnostics in order.
