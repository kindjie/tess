# TDD: Modern C++ / Compile-Time Safety

## 1. Summary

This document defines the modern C++ strategy.

The library is performance-first and compile-time-shape-first. World shape, chunk shape, key layout, field schemas, movement/topology vocabulary, operation schemas, and many capabilities should be known at compile time.

## 2. Goals

- Use constexpr/consteval configuration.
- Use concepts for readable errors.
- Use typed field handles.
- Validate operation schemas at compile time.
- Validate topology/movement vocabularies at compile time.
- Use source_location.
- Use expected-style recoverable errors.
- Support Clang-first development.
- Avoid runtime polymorphism in hot paths.
- Enable compile-time GPU/backend integration.
- Keep C++26 features optional.

## 3. Non-goals

- No runtime world dimensions in core.
- No required C++26 reflection/contracts.
- No required exceptions in hot paths.
- No dynamic field access in hot loops.
- No required virtual storage/path/GPU interfaces.

## 4. Language baseline

Required:

- C++20 minimum
- C++23 preferred
- upstream Clang primary
- Apple Clang where feasible

Use:

- concepts
- requires
- constexpr/consteval
- NTTP-friendly shape config
- std::span
- std::source_location
- std::expected or fallback
- std::mdspan or fallback
- [[nodiscard]]
- [[no_unique_address]]
- feature-test macros

Optional:

- std::stacktrace
- std::format/std::print
- C++26 pack indexing
- user-generated static_assert messages
- delete with reason
- contracts/reflection later

## 5. Compile-time world shape

World shape derives chunk counts, key widths, effective dimensions, residency mode, topology capabilities, and product defaults.

Invalid shapes fail at compile time.

## 6. Field schema

Fields are compile-time declarations with type, name/id, persistence, alignment, and policies. Validate duplicate names/ids, invalid types, serialization, alignment, size, and missing required fields.

## 7. Typed field handles

FieldHandle<T> encodes field tag, type, storage properties, access policy, and debug name. String lookup is setup/tool/debug only.

## 8. Operation schemas

Operations declare reads, writes, domain, write policy, backend eligibility, scratch needs, deterministic behavior, and result type. Validate declared fields and hazards.

## 9. Concepts

Concepts include ShapeLike, FieldTag, FieldSchemaLike, TileWorld, BlockKernel, QueuedOperation, TransitionProvider, MovementVocabulary, TopologyRules, PathHeuristic, OpenSet, FieldProductStorage, EntityHandleAdapter, PositionAdapter, GpuBackend, GpuAlgorithmProvider, DebugPanelProvider.

## 10. Declarative topology DSL

Provide compile-time DSL for tags, movements, movement classes, passability rules, and transitions. Compile to bit assignments, masks, transition tables, schema hash, debug names, and validation metadata.

## 11. Compile-time GPU backend

GPU backend is compile-time polymorphic. Game/plugin provides backend satisfying concept. Substrate provides descriptors/planner/product cache/diagnostics. Backend provides resource and command integration.

## 12. Error handling

Use expected-style results for recoverable errors, compile-time errors for invalid static config, and debug assertions for invariants. Avoid exceptions in hot paths.

## 13. Diagnostics support

Use source_location and structured warnings. Diagnostic levels compile out via if constexpr where possible.

## 14. Static assertions

Static assertions should be specific:

- invalid chunk power of two
- size not divisible
- key overflow
- duplicate field
- invalid operation read/write hazard

Use C++26 richer messages where available.

## 15. Memory and allocation safety

Expose static memory estimates and debug runtime checks. Derived products use explicit owners, generational handles, byte-budgeted caches, arenas/pools, and no shared ownership graphs.

## 16. Lifetimes and handles

Use generational handles for path tickets, field products, GPU products, cached paths, result channels, and residency dependencies. Debug validates stale handles.

## 17. Determinism

Support deterministic mode with stable ordering, tie-breaking, budgets, reductions, cache eviction, and no nondeterministic authoritative GPU products.

## 18. Clang tooling

Use clang-tidy, Clang Static Analyzer, ASan, UBSan, TSan where practical, warnings-as-errors, and future custom clang-tidy checks.

Potential checks:

- field_by_name in hot path
- AllTiles on huge sparse world
- unsafe API outside allowed files
- allocating terminal in hot phase
- ignored expected result

## 19. API style rules

Prefer named compile-time declarations, typed handles, explicit domains/policies, expected errors, source_location defaults, constexpr traits, and concept-constrained extension points.

Avoid bool soup, runtime strings in hot paths, hidden allocation, hidden materialization, hot virtual calls, unconstrained templates, and implicit full-world domains.

## 20. Tests

Compile-time tests for shapes, field schemas, op schemas, topology DSL, GPU backend, ECS adapter. Runtime tests for expected errors, stale handles, source_location, diagnostics, cache invalidation, deterministic ordering.

## 21. Benchmarks

Measure static vs branchy reference if available, TileKey u64/u128, diagnostics levels, source_location, stacktrace, compile time, binary size, field handle vs string lookup.

## 22. Acceptance criteria

- Shape/chunk compile-time validated.
- Field schemas typed and validated.
- Operation schemas catch common errors.
- Topology vocabulary declarative and compile-time.
- TileKey layout derived and validated.
- GPU backend compile-time polymorphic.
- Diagnostics configurable by level.
- Concepts produce readable errors.
- Clang CI catches warnings/sanitizers/misuse patterns.
