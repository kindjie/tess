- `DiagnosticsSnapshot` no longer promises that all of its members are
  copies. `TraceRecord::label` is a `std::string_view`, and the trace API
  separately requires a label to outlive every reader — so the export
  header told consumers they had no lifetime obligation while the trace
  header said they did, and the ImGui panels dereference that view. The
  contract now states the exception, and what satisfies it.
- `path/node_index_space.h` moves from the public header set to the
  implementation set, where its detail siblings already are. Its entire
  body is `namespace tess::detail`, and it has no public-surface entry. Its
  primary-template assertion also told callers that the `SparseResident`
  mapping "lands in a later slice"; that specialization is in the same
  file, so the message now describes the real condition — a custom
  residency policy needs its own specialization.
