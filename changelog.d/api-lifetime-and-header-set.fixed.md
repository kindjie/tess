- `DiagnosticsSnapshot` no longer promises that all of its members are
  copies. `TraceRecord::label` is a `std::string_view`, and the trace API
  separately requires a label to outlive every reader — so the export
  header told consumers they had no lifetime obligation while the trace
  header said they did, and the ImGui panels dereference that view. The
  contract now states the exception, and what satisfies it.
- `path/node_index_space.h` moves from the public header set to the
  implementation set, where its detail siblings already are. Its entire
  body is `namespace tess::detail`, so it declares nothing a consumer can
  name — but `tess.h` and `path/path.h` do include it, and both header
  sets install to the same paths, so the file still ships and every
  existing include of it still resolves. The one behavioural consequence
  is inside the build: `INTERFACE_HEADER_SETS_TO_VERIFY` covers the public
  set only, so the header is no longer standalone-compiled on its own. It
  stays covered transitively through `tess.h`, which is swept. Its
  primary-template assertion also told callers that the `SparseResident`
  mapping "lands in a later slice"; that specialization is in the same
  file, so the message now describes the real condition — a custom
  residency policy needs its own specialization.
- `save_world_archive` and `load_world_archive` are now `[[nodiscard]]`,
  matching `inspect_world_archive`. In an exception-free library their
  returned `WorldArchiveResult` is the only failure channel, so
  `save_world_archive(world, out);` previously compiled and silently
  discarded a failed save.
