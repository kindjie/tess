# tess_grid_map_generators_test

- `tess_grid_map_generators_test`: pins deterministic procedural maps and their
  Moving AI serialization. A contract sweep covers every accepted extent pair
  from 8x8 through 64x64 for both generators, rather than sampling them, and
  requires full connectivity. Every generated oracle case compares tess A*
  exactly with an independent Dijkstra implementation in both directions.
