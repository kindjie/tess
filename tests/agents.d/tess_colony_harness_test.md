# tess_colony_harness_test

- `tess_colony_harness_test`: pins a 100-agent production-stack scenario and
  its churn, flow-accounting, determinism, and cross-configuration invariants.
  The serial/pool comparison also asserts `pool_phases`, so an idle pool cannot
  pass; the serial-only outcome golden catches drift shared by every compared
  configuration. Incremental topology is compared with a fresh rebuild before
  agents move after each churn event, not only at the end.
