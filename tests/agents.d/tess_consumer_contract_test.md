# tess_consumer_contract_test

- `tess_consumer_contract_test`: the consumer contract (redesign
  section 10 item 5). Three probe translation units in
  `tests/consumer_contract/` include the public header set in
  declaration order, reverse order, and leaf-first with no umbrella,
  and are linked together, so a lost `inline`, a non-inline
  namespace-scope definition, or a header that silently depends on a
  sibling include fails the build before any assertion runs. The test
  then compares tess's own identity facilities (`tag_identity`,
  `planned_world_stamp`) across translation units — a per-unit static
  would corrupt anything using them as cache keys and is invisible to
  a single-unit test. Companion build-only targets:
  `tess_verify_interface_header_sets` (every public header compiled
  standalone; implementation fragments excluded because they `#error`
  outside their owning header) and `tess_contract_cells` (the
  macro-configuration matrix: bare, NDEBUG, diagnostics, EnTT-only,
  Flecs-only, ImGui, WebGPU — the mixed-adapter cells being coverage
  no preset provides). Each cell is an executable built from two
  identically configured translation units, not an object library: it
  takes a link step for a definition that should be inline and is not
  to surface as a duplicate symbol, which is how the diagnostics cell
  covers the `inline thread_local` counter pointers that every other
  probe preprocesses away.
