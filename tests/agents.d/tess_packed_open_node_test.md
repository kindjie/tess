# tess_packed_open_node_test

- `tess_packed_open_node_test`: pins the packed open-list key as exactly
  isomorphic to the former three-field ordering, with differential heap checks
  and tie-heavy goldens captured from every packed consumer before the change.
  Scratch reuse deliberately follows a goal early-exit, when live heap entries
  remain, and the reserved warm search must allocate nothing.
