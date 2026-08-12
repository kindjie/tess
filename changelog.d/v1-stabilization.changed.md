- Defined the enforceable 1.x source-compatibility boundary with an exhaustive
  installed-header manifest, named request/options/handle APIs, fail-fast
  lifetime and worker-pool misuse checks, prerelease-aware CMake selection,
  declaration-level compatibility and package evidence, release-tag-anchored
  snapshots, checksum-aware deep archive fuzzing, and an upgrade guide.
  Compatibility snapshots must exactly match a new release before becoming
  immutable and preserve aggregate status (including public-base aggregates),
  inherited-constructor absence, fixed public fields, conditional declarations
  and access, append-only enumerators, and existing overload sets. Worker-pool
  dispatch ownership now covers plan-ordered result selection, and
  `DeltaCollector` self-moves and repeated moves invalidate or poison borrowed
  frames fail-closed.
