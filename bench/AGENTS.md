# Benchmarks

- The benchmark binaries (`tess_bench`, `tess_bench_diagnostics`) enforce
  correctness checks after their timed loops via an aborting `bench_check`
  helper (endpoints, legal unit steps onto passable tiles, expected costs,
  agent/frame stats, cache outcomes). `tess_bench_diagnostics` additionally
  asserts the warm `path/astar_open_2d` iteration performs zero allocations.
  The resolved-transition baseline family additionally times open diagonal,
  axial-hex, and stair-provider exact searches and validates every returned
  edge through the same resolved model outside the timed loop.
- Benchmark workflow, threshold manifests, and the paired sentinel protocol
  are documented in `CONTRIBUTING.md`; new or renamed benchmarks must stay
  coherent with `bench/workload-matrix.json`
  (`tools/check_workload_matrix.py`).
