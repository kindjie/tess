- Added `World::mark_content_changed` to invalidate version-keyed derived
  state without creating dirty-mask work, with identical dense and sparse
  semantics and stale dirty-observation refusal.
