- Defined the enforceable 1.x source-compatibility boundary with an exhaustive
  installed-header manifest, named request/options/handle APIs, fail-fast
  lifetime and worker-pool misuse checks, prerelease-aware CMake selection,
  declaration-level compatibility and package evidence, release-tag-anchored
  snapshots, checksum-aware deep archive fuzzing, and an upgrade guide.
  Compatibility snapshots must exactly match a new release before becoming
  immutable and preserve aggregate status (including public-base aggregates),
  inherited-constructor absence, fixed public fields, conditional declarations
  and access, branch-aware append-only enumerators, and existing overload sets.
  Worker-pool dispatch ownership now covers plan-ordered result selection;
  even empty nested dispatches fail fast. Released snapshot directories are
  append-only, constructor and data-member detection covers parenthesized
  specifiers, pre/post-name annotation macros, destructor identity, and
  parameter-type spellings and anonymous type objects; conditional member and
  base-clause aggregate availability, attributed and base-imported named or
  operator overloads (including relational and compound operators), inherited
  constructors, relational template expressions, requires-expressions, and
  elaborated parameter types, including unparenthesized relations,
  parameterless requirements, dependent bases, and annotated types; stable
  nested calls, trailing constraints, parenthesized pointer/reference/array
  data, and relational dependent-base arguments retain the correct identity;
  stable macro redefinitions/undefinitions cannot evade compatibility checks,
  future
  unmerged tags do not constrain maintenance
  branches, release evidence retains checksummed actual-version job logs,
  release package checks use C++20 without an unavailable compiler launcher,
  the MSVC 19.44 floor check fails closed, and
  `DeltaCollector` self-moves and repeated moves invalidate or poison borrowed
  frames fail-closed.
