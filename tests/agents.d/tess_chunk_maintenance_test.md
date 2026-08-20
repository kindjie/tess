# tess_chunk_maintenance_test

- `tess_chunk_maintenance_test`: pins the stable external chunk adapter's
  dense and sparse slot bindings, dirty-mask ownership,
  shared content-version drift repair, strongly typed generation-safe product
  tokens, explicit
  quiescent residency transitions, retained retry debt under bounded FIFO and
  coalescing capacity, structural-backend synchronous scheduling outside
  budgeted debt reconciliation, nested synchronous debt handoff,
  capacity/retry/exception behavior through experimental comparison backends,
  deterministic backend equivalence, canonical world-archive-v2 independence,
  warmed allocation behavior, and release lifetime. Sparse residency changes
  remain adapter-exclusive after binding; producer/drain concurrency is
  exercised
  only where world access is externally synchronized or scheduling does not
  touch the world.
