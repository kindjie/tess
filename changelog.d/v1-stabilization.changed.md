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
  nested calls (including trailing template arguments), trailing constraints,
  cv-qualified parenthesized pointer/reference/array data, grouped function
  declarators (including annotations and relational template headers),
  function-pointer return types, and qualified relational dependent-base
  arguments retain the correct identity without confusing namespace-named,
  nonterminal, or nested template-argument members; pointer arrays with
  arbitrary parenthesized constant-expression bounds remain data, while
  annotated grouped pointer/reference returns retain their callable name;
  parenthesized arrays and annotated object/pointer declarators remain data;
  terminal dependent or nondependent template bases remain inherited
  constructors after single, nested, or terminal relational arguments without
  confusing intervening non-template components, nested arguments, or later
  nonterminal components, while qualified same-name and namespace-qualified
  member-pointer object declarations remain data and do not suppress aggregate
  compatibility evidence; bare and conditional `explicit` constructors remain
  aggregate-breaking; elaborated qualified objects remain data and template
  names nested in dependent `decltype` expressions or spaced nested template
  arguments cannot impersonate an owning type; constructor templates and
  leading constraints preserve constructor identity, relational comma-separated
  base clauses preserve aggregate evidence in either access ordering without
  splitting ordinary multi-argument base templates, and lexically ambiguous
  using-imports are checked conservatively as both constructors and ordinary
  overloads whether they occur in the snapshot or current sources; complete
  namespace/class using-declarator lists and inherited callable hiding through
  direct, chained alias, alias-template, namespace-import, transitive, or
  dependent template bases are also rejected across supported headers, while
  inaccessible and namespace-distinct bases remain independent; scope-aware
  namespace/type aliases, redeclaration-equivalent signatures, template
  constraints, and member qualifiers avoid false positive or missed overloads;
  stable macro redefinitions/undefinitions cannot evade compatibility checks,
  future
  unmerged tags do not constrain maintenance
  branches, release evidence retains checksummed actual-version job logs,
  release package checks use C++20 without an unavailable compiler launcher,
  the MSVC 19.44 floor check fails closed, and
  `DeltaCollector` self-moves and repeated moves invalidate or poison borrowed
  frames fail-closed.
