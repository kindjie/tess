- The pathfinding guide no longer restates A* timings that the
  performance page had already corrected; it defers to that page, which
  records the measurement conditions alongside the figures.
- The strategy-comparison timing table, whose original run's commit and
  toolchain were never recorded, is regenerated from commit `d653d813`
  on two platforms — Apple M3 Max and Steam Deck — with conditions and
  raw outputs retained. The unprovenanced ratios reproduced within
  0.2, and the ordering held on both architectures.
