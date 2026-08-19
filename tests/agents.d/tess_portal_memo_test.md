# tess_portal_memo_test

- `tests/tess_portal_memo_test.cc`: the memo's key omits the goal, the
  world and the movement class, so this file exists to pin the two
  mechanisms that make the omission sound rather than to exercise the
  table: the generation stamp and the RAII selection scope. A change
  that keeps entries alive across selections, or that lets a nested
  selection share a generation, is the failure these tests are for.
- Saturation cases are written against `PortalMemo::kCapacity` rather
  than a literal, so re-sizing the table does not silently stop testing
  the boundary.
