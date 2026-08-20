# tess_maintenance_contract_ndebug_test

- `tess_maintenance_contract_ndebug_test`: compiles and runs the registered
  maintenance contract tests with `NDEBUG`; the duplicated death tests prove
  wrong-owner, stale, setup, task-lifetime, and non-idle release misuse remains
  fail-fast when debug assertions are absent.
