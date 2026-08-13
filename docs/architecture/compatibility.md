# Compatibility evidence

Tess uses several independent evidence layers for the 1.x source-compatibility
contract. No repository-maintained parser attempts to model C++ declarations,
lookup, overload resolution, aggregate rules, or preprocessing semantics.

Compatibility snapshots provide mechanical, append-only evidence at RC1 and
each stable minor release. They record:

- stable and optional-stable header membership;
- direct stable-aggregate includes;
- documented public namespace-scope and `TESS_*` macro names per header;
- one canonical installed-package consumer project with distinct source and
  archive-loader executables; and
- archive-v1 fixtures with strict producer and schema metadata.

The snapshot checker confines portable POSIX-relative paths to their snapshot,
requires the canonical CMake project byte-for-byte, and compares released
snapshots with their release tags. Per-header name inventories are inexpensive
removal tripwires; they are not a semantic representation of the C++ API.

Compiled immutable consumers carry the source-level evidence for signatures,
defaults, fields, overload resolution, aggregate initialization, and supported
macro configurations. Installed-package and optional-integration builds, the
supported toolchain matrix, archive loads, and release review provide the
remaining evidence. The current compatibility promises and exclusions are in
[the support policy](../support.md).
