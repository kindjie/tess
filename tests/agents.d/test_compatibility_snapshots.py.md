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
  overload identity (including dependent bases), parenthesized and bare
  relational template expressions, parameterized and parameterless trailing
  requires-expression bodies, elaborated parameter and return types,
  function-like type/enum annotations, nested calls in fallback declarations,
  trailing constraints, parenthesized object, pointer, reference, array,
  member-pointer, nested, and callable-template data declarators, grouped
  function declarators, qualified grouped and bare relational dependent-base
  arguments, namespace-named imports, trailing return and constraint template
  calls, and post-type cv/storage specifiers,
  constructor-like parameter spellings, and per-configuration
  member and base-clause aggregate availability, anonymous type objects,
  stable-macro redefinition and undefinition, release-tag byte and directory
  immutability (excluding future unmerged tags), installed-package consumer
  metadata, and the exact RC1 snapshot requirement. It intentionally uses a
  minimal synthetic repository; compiling consumers and loading archives are
  separate CMake/runtime release gates.
