# tess_path_runtime_test

- `tess_path_runtime_test`: pins ticket, lifecycle, cache, batching, policy,
  failure-stat, and allocation contracts for the path request runtime. Seeded
  repeated-goal grouping is checked against per-request A* and an independent
  computation of every grouping counter. Oversized field products are rejected
  before a duplicate build, and out-of-shape starts before unchecked key
  conversion. Borrowed result and lookup spans remain valid across unrelated
  cache growth; an identical warm processing pass allocates nothing. A
  movement-class
  field edit announced only through `mark_content_changed` invalidates cached
  routes without leaving dirty work behind.
- Checked ticket lookup returns values only from a fully published processing
  pass. The throwing-provider fixture first completes a trivial request, then
  interrupts the next one to prove neither a partial result nor an uninstalled
  borrowed path becomes visible.
