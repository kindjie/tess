## 2026-08-12 - Keep C++ semantics in compiled compatibility evidence

- Removed the handwritten C++ declaration parser and its grammar-edge test
  corpus from the compatibility snapshot gate. Maintaining a second,
  incomplete C++ frontend in Python created false confidence and repeated
  source-compatibility false positives and negatives.
- Evaluated Tree-sitter and Clang ExtractAPI as replacements. Tree-sitter could
  not model valid declarations split across preprocessing branches. ExtractAPI
  owned the language semantics but emitted compiler- and standard-library-
  specific spellings, so an immutable cross-platform source snapshot would not
  be portable without another normalization language.
- Kept snapshots deliberately mechanical: header classes, direct aggregate
  membership, documented public namespace-scope names, public `TESS_*` macro
  names, consumer/archive metadata, and release-tag immutability. C++
  signatures and behavior are evidence from compiled immutable consumers,
  optional-integration builds, the toolchain matrix, and release review rather
  than a repository-maintained parser.
