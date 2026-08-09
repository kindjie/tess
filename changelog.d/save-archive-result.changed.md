- `save_world_archive` now returns `WorldArchiveSaveResult` instead of the
  shared `WorldArchiveResult`. The shared type advertises the full 14-value
  `WorldArchiveStatus` enum, and save never wrote it — the field was
  invariantly `Ok`, so every symmetric `if (result.status != Ok)` a caller
  wrote against it was dead code. Worse than dead: it implied save had a
  failure channel, so callers did not guard the failure mode it actually
  has, which is the output vector exhausting memory. That is not
  representable as a returned status, so the new type states the contract
  at the declaration rather than implying a false one.
- `bytes_processed` becomes `bytes_written` on the save result. Source-breaking.
- 21 test sites asserted `save_world_archive(...).status == Ok`, which
  could not fail. They now assert `bytes_written > 0`, which can: a save
  that silently produced nothing would have passed the old form.
