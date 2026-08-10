# tess_counter_golden_probe

- `tess_counter_golden_probe`
  (`tests/tess_counter_golden_probe.cc`): gtest-free probe behind the
  shadow-mode counter goldens (redesign section 3.3). Nine fixed
  serial workloads — unit and weighted serpentine A*, unit product
  replay, weighted product nearest-target, queued serial update with
  dirty merge — run under scoped diagnostics sinks, pin their exact
  functional outcomes (costs, node counts, targets, written fields,
  merged counts, versions), and emit observed
  `PathCounters`/`QueuedPhaseCounters` JSON for the checker. Registered
  as the `TessCounterGoldenProbe`/`TessCounterGoldenCheck` ctest
  fixture pair; drift is advisory until the strict promotion.
