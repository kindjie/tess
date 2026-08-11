# tess_sim_schedule_test

- `tess_sim_schedule_test`: pins phase ordering, tick-based cadence, dirty,
  event, manual and background triggers, disablement, exceptions, sealing, and
  warm allocation behavior. Cadences count fixed ticks rather than frames and
  stay aligned through speed changes, backlog, pause, disablement, and throws.
  A throwing callback restores consumed triggers without rolling back the
  clock. Explicitly `noexcept` tasks retain the no-throw erased signature.
