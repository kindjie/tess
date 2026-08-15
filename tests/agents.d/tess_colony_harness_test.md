# tess_colony_harness_test

- `tess_colony_harness_test`: pins a 100-agent production-stack scenario and
  its churn, flow-accounting, determinism, and cross-configuration invariants.
  The serial/pool comparison also asserts `pool_phases`, so an idle pool cannot
  pass; the serial-only outcome golden catches drift shared by every compared
  configuration. Incremental topology is compared with a fresh rebuild before
  agents move after each churn event, not only at the end. The
  placement-stride test pins the default stride-8 seating ceiling at exactly
  227 agents on the shared map (500 requested → 273 `agents_unplaced`) and
  proves stride 2 seats 500; the budgeted-progress mixed bench relies on that
  capacity for its population ladder. The PIBT-tier golden doubles as the
  dispatch witness (`pibt_passes` nonzero) and pins a step count that
  differs from the baseline golden's, proving the tiers diverge on the
  shared fixture.
