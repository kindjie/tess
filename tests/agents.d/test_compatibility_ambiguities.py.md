# test_compatibility_ambiguities.py

- Covers conservative compatibility evidence for lexically ambiguous C++
  using-imports and relational base clauses, including ordinary multi-argument
  public base templates, full class/namespace using-declarator lists, alias and
  dependent/transitive inherited callable hiding, namespace imports,
  inaccessible/namespace-distinct base exclusions, cross-header callable and
  macro identities, namespace/type alias scope and qualification, canonical
  redeclaration signatures including complex/template parameters, constraints,
  and member qualifiers, root-scope imports, member type aliases, elaborated or
  unnamed-array parameter types, C++ array/function parameter adjustment,
  nested and parenthesized declarator identity, named top-level-cv pointers,
  line splicing, and symmetric historical/current callable identities.
