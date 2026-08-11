# tess_topology_test

- `tess_topology_test`: broad region-label, portal, reachability, index, shape,
  and incremental-update coverage for dense topology. CSR reachability is
  compared with an independent portal-scan BFS. Every seeded edit compares the
  incremental graph with a full rebuild; failures before mutation preserve the
  graph, while failures after mutation begins clear all derived state and its
  freshness stamp rather than exposing a torn graph.
