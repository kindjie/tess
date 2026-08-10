# tess_grid_map_generators_test

- `tess_grid_map_generators_test`: deterministic procedural map
  generators for the S1 scenario layer (`tests/grid_map_generators.h`
  — recursive-division maze and room-and-corridor, SplitMix64 integer
  PRNG, strict Moving AI text emission). Pins extent/parameter
  contract rejection, byte-identical regeneration, the exact
  SplitMix64 stream, a stable tiny-map golden, full passable
  connectivity via a single BFS flood, guaranteed first-room
  placement, deterministic endpoint sampling (farthest-pair
  inclusion), rejection of room extents whose margin check would
  overflow, a contract sweep asserting that every accepted extent
  pair from 8x8 to 64x64 parses and is fully connected for both
  generators (about 6500 maps, 3 s under ASan), and the S1 oracle
  leg: tess A* equals the independent Dijkstra reference exactly,
  Orthogonal and DiagonalBothClear, both directions, on every
  generated case.
