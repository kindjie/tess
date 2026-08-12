# test_compatibility_snapshots.py

- `tests/test_compatibility_snapshots.py`: pins earlier-snapshot superset
  checks, declaration-level source compatibility (including conditional
  declarations, access labels, enums, inherited constructors, and C++20
  derived aggregates), exhaustive conditional-enum alternatives and implicit
  positions, branch-specific visibility, attributed/conditional-explicit
  constructors (including pre/post-name function-like and object-like
  annotation macros), parenthesized data-member specifiers, attributed callable
  and destructor identities, named, symbolic, relational, call, subscript, and
  conversion or compound operator identities/imports, inherited-constructor
  overload identity, relational template expressions, trailing
  requires-expression bodies, elaborated parameter types, constructor-like
  parameter spellings, per-configuration
  member and base-clause aggregate availability, anonymous type objects,
  stable-macro redefinition and undefinition, release-tag byte and directory
  immutability (excluding future unmerged tags), installed-package consumer
  metadata, and the exact RC1 snapshot requirement. It intentionally uses a
  minimal synthetic repository; compiling consumers and loading archives are
  separate CMake/runtime release gates.
