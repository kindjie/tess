# 1.x compatibility snapshots

Create an immutable directory named for the complete release version at RC1
and at every stable minor release. Each directory contains:

- `manifest.json`, recording stable and optional-stable headers, direct stable
  aggregate membership, extracted public namespace-scope and `TESS_*` macro
  names, the consumer project and its two executable/test target names, and
  archive fixture metadata;
- a CMake consumer project that uses stable headers only, discovers the
  candidate installation with `find_package(tess CONFIG REQUIRED)`, links
  `tess::tess`, and registers both source and archive consumers as tests; and
- canonical archive-v1 fixtures with producer version and fixed-schema
  metadata, plus one `archive_consumer` source that loads every listed fixture
  when passed the snapshot directory.

`tools/check_compatibility_snapshots.py` preserves header classes, direct
aggregate membership, documented public namespace-scope names, and public
`TESS_*` macro names. It also confines recorded paths to the snapshot, verifies
their portable POSIX-relative spelling, verifies fixture metadata, and compares
released snapshots byte-for-byte with their `v<version>` tags.

The checker deliberately does not parse declaration signatures or model C++
semantics; it reuses the documentation gate's namespace-name scanner.
Consumer `CMakeLists.txt` files use one canonical generated form so the checker
can compare them exactly instead of interpreting the CMake language.
Signatures, defaults, aggregate use, fields, overload resolution, macro
configurations, and other language semantics are protected by compiling the
immutable consumer projects against the candidate package, exercising optional
integrations, and reviewing API changes. The name inventory is an inexpensive
tripwire, not a standalone proof of the full support policy.

RC1 and stable `1.x.0` source versions fail the checker until their matching
snapshot exists and exactly matches the current inventories. Pre-1.0
development does not fabricate a future snapshot. The snapshot matching the
version being prepared may precede its tag; every older snapshot must already
have a matching reachable release tag. Snapshot directories are append-only:
never edit a released snapshot to make a compatibility failure disappear.
