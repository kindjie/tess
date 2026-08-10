# tess_packed_open_node_test

- `tess_packed_open_node_test`: pins the packed open-list key that
  orders every weighted search's heap — comparator isomorphism with the
  legacy three-field ordering over corner and seeded random triples,
  pack/unpack round trips at all corner values, a differential heap test
  requiring identical pop sequences on adversarial insertions
  (same-index stale chains, equal-(f,g) index ties, boundary words),
  tie-heavy goldens for every packed consumer captured from the
  pre-change heap loops — weighted A* (cost, path length, path-walk
  validity, expanded/reached literals), the general flood (expanded/
  reached and replay-cost literals), the boxed flood, the weighted
  goal-set product, and the stair-provider product branch (the one
  packed consumer a plain fixture cannot reach; expanded/reached
  literals each) — scratch reuse across weighted/unit/weighted
  searches after a goal early-exit leaves live heap entries, and warm
  allocation-free weighted search after reserve_nodes.
