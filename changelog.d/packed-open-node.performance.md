- Weighted searches order their open lists with a packed 64-bit key
  (one compare instead of up to three field compares per heap step).
  The ordering is bit-identical — a strict total order isomorphic to
  the previous comparator — so paths, costs, expansion counts, and
  determinism are unchanged. On Apple M3 the weighted A* batch cells
  drop 13-14% and the weighted distance-field batch 11%.
