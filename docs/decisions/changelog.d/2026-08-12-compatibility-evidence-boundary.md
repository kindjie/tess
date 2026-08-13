## 2026-08-12 - Keep C++ semantics in compiled compatibility evidence

- Removed the handwritten C++ declaration parser and its grammar-edge test
  corpus from the compatibility snapshot gate. Maintaining a second,
  incomplete C++ frontend in Python created false confidence and repeated
  source-compatibility false positives and negatives.
- Evaluated a concrete-syntax parser and a compiler-owned API extractor as
  replacements. The syntax parser could not model valid declarations split
  across preprocessing branches. The compiler extractor owned the language
  semantics but emitted compiler- and standard-library-specific spellings, so
  an immutable cross-platform source snapshot would not be portable without
  another normalization language.
- Kept snapshots deliberately mechanical: header classes, direct aggregate
  membership, per-header documented public namespace-scope and `TESS_*` macro
  names, consumer/archive metadata, and release-tag immutability. C++
  signatures and behavior are evidence from compiled immutable consumers,
  optional-integration builds, the toolchain matrix, and release review rather
  than a repository-maintained parser.
