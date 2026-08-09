- 107 public functions returning a status or result type are now
  `[[nodiscard]]`, closing the gap the audit named: 529 sites already had
  the attribute, so these were the exceptions rather than the rule.
  Because the library is exception-free, the returned value is the only
  failure channel — `load_world_archive(world, bytes);` as a bare
  statement silently ignored `Corrupt`, and
  `build_region_graph(world, scratch, graph);` silently ignored a graph
  that never built. Source-breaking for callers that discard these
  values under `-Werror`, which is why it lands before 1.0 rather than
  after.
- The sweep found three discards inside the project's own code. Two are
  deliberate and now say so: `update_region_graph` discards the
  per-chunk build status in both its incremental branches, sound in the
  dense branch because `MissingChunk` cannot arise under
  `AlwaysResident`, and in the sparse branch because the
  residency-generation check has already diverted every eviction to a
  full rebuild. The third was a documentation defect: the
  `getting-topology` example — reproduced in `getting-started.md` and
  `guide/topology.md` — built a region graph and pathed against it
  without checking that it built, teaching exactly the habit this
  change is meant to remove.
- The colony examples and the shared test harness now check the same
  status. In the harness the check is not cosmetic: a region graph that
  failed to build leaves every downstream reachability answer
  meaningless while the run still reports a result, so 45 test setup
  sites that could previously pass vacuously now assert.
