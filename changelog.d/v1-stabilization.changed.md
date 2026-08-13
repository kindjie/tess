- Defined the enforceable 1.x source-compatibility boundary with an exhaustive
  installed-header manifest, named request/options/handle APIs, fail-fast
  lifetime and worker-pool misuse checks, prerelease-aware CMake selection,
  package evidence, release-tag-anchored snapshots, checksum-aware deep archive
  fuzzing, and an upgrade guide.
- Compatibility snapshots preserve header classes, direct aggregate
  membership, documented public namespace-scope names, consumer/archive
  metadata, and release-tag immutability without maintaining a handwritten C++
  parser. Compiled consumers and integration builds provide the evidence for
  signatures, defaults, fields, overload resolution, aggregate use, and macro
  configurations.
- Release snapshots are append-only and tag-anchored. Release evidence retains
  checksummed actual-version job logs, package validation uses C++20 without an
  unavailable compiler launcher, and compiler-floor checks fail closed.
- Worker-pool dispatch ownership covers plan-ordered result selection, even
  empty nested dispatches fail fast, and `DeltaCollector` self-moves or repeated
  moves invalidate or poison borrowed frames fail-closed.
