## 2026-08-15 - Separate content-version changes from dirty notification

- Added `World::mark_content_changed` for authoritative field writes that
  must invalidate version-keyed derived state but do not concern a dirty-mask
  consumer. It increments only the chunk content version and therefore also
  makes an earlier `DirtyObservation` stale.
- Kept notification ownership explicit: content-only notification does not
  set dirty flags or bounds, advance topology freshness, or wake schedule
  tasks. Callers use the corresponding dirty-metadata, topology, and schedule
  notification protocols when those consumers must observe the edit.
- Shared the metadata mutation between dense and sparse worlds so their
  version semantics cannot diverge. Sparse residency generations and LRU
  state remain unchanged.
- Kept synchronization external. The generation check protects a maintenance
  pass from clearing after a serialized intervening change; it does not make
  simultaneous unsynchronized world mutation thread-safe.
