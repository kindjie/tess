## 2026-08-10 - Packed open node on the Deck: flat, and why that gates d-ary

- Area: on-device verification of the packed open-list key (merged
  2026-08-10), which the M3 A/B accepted at -13.5/-14.0/-13.4% on the
  weighted A* batch cells and -11.3% on the flood batch.
- Method: interleaved A/B/A/B on the Steam Deck (performance governor,
  2 repetitions x 2 rounds), pre-packed (4a22164) against merged main
  (bda7afa) — library code between those commits differs only by the
  packed node. Same five cells as the M3 run plus the watch cells.
- Evidence: FLAT on every cell — mixed 1025.3 to 1029.8 ms, shared
  472.9 to 474.0 ms, multigoal 901.0 to 899.3 ms, flood 263.2 to
  262.9 ms, watch cells unmoved. All deltas within round-to-round
  noise. No regression; the M3 result simply does not transfer, the
  mirror image of the seam-scan hoist (Deck-only, M3-flat). The
  recorded non-transfer caveat (libc++ versus libstdc++ heap
  baselines) is now measured fact in both directions.
- Attribution: a same-session srcline profile of the mixed batch cell
  on the packed binary shows where the Deck's cost actually sits:
  libstdc++ sift machinery (stl_heap.h lines) ~20% of the cell, the
  packed key compare (path.h:675) ~8%, accessors/comparator wrappers
  ~3% — about 30% of the cell still in open-list work after packing.
  The packed key cheapened the compare, which is what M3 was paying;
  the Deck pays for sift depth and its memory traffic, which the key
  does not change.
- Decision: the d-ary sift follow-up is now JUSTIFIED by direct
  evidence on the platform that did not benefit — halving sift levels
  attacks the ~20% the Deck still spends there. It remains a separate
  experiment with its own A/B on both platforms; the pop-sequence
  invariance argument and the differential/golden test harness from
  the packed-node change carry over unchanged.
