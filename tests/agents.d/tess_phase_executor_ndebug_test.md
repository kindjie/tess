# tess_phase_executor_ndebug_test

- `tess_phase_executor_ndebug_test`: recompiles the phase-executor contract
  with `NDEBUG`; its death tests prove nested dispatch and reserve during an
  active worker-pool dispatch remain release-mode fail-fast checks.
