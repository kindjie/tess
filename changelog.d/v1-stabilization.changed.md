- Defined the enforceable 1.x source-compatibility boundary with an exhaustive
  installed-header manifest, named request/options/handle APIs, fail-fast
  lifetime and worker-pool misuse checks, prerelease-aware CMake selection,
  declaration-level compatibility and package evidence, release-tag-anchored
  snapshots, checksum-aware deep archive fuzzing, and an upgrade guide.
  Compatibility snapshots must exactly match a new release before becoming
  immutable and preserve aggregate status, fixed public fields, append-only
  enumerators, and existing overload sets. `DeltaCollector` self-moves and
  repeated moves now invalidate or poison borrowed frames fail-closed.
