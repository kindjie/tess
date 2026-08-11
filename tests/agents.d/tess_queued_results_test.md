# tess_queued_results_test

- `tess_queued_results_test`: pins result-channel and cooperative-async state,
  ordering, generation, execution, retry, reentrancy, and allocation contracts.
  A throwing drain leaves its slot available for exactly one retry even if the
  callback grows capacity; reentrant clear must not allow a stale recovery
  write. Serial early-stop tails remain `Pending`, whereas threaded executors
  finish the issued range by contract.
