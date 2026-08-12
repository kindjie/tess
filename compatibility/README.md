# 1.x compatibility snapshots

Create an immutable directory named for the complete release version at RC1
and at every stable minor release. Each directory contains:

- `manifest.json`, recording stable and optional-stable headers, direct stable
  aggregate membership, extracted public symbol names, normalized public API
  declarations, the consumer project and its two executable/test target
  names, and archive fixture metadata;
- a CMake consumer project that uses stable headers only, discovers the
  candidate installation with `find_package(tess CONFIG REQUIRED)`, links
  `tess::tess`, and registers both source and archive consumers as tests; and
- canonical archive-v1 fixtures with producer version and fixed-schema
  metadata, plus one `archive_consumer` source that loads every listed fixture
  when passed the snapshot directory.

`tools/check_compatibility_snapshots.py` verifies that current sources retain
every prior header in its original compatibility class, every direct aggregate
member, public symbol, and normalized declaration contract. Declaration
contracts retain function signatures and defaults, public members, enum
values, aliases, concepts, constants, and configuration macro definitions.
Existing types cannot gain public data members, and direct aggregate imports
must remain uncommented and unconditional.
The checker also confines every recorded path to its snapshot, verifies fixture
metadata, and compares every earlier snapshot byte-for-byte with its
`v<version>` release tag. Normal CI fetches those tags and runs the source
superset and immutability checks. Release CI additionally installs the
candidate package, then configures, builds, and runs both named tests from each
immutable consumer project against that installation. Snapshot directories
are append-only: never edit a released snapshot to make a compatibility
failure disappear.

RC1 and stable `1.x.0` source versions fail the checker until their matching
snapshot exists. Pre-1.0 development does not fabricate a future snapshot.
The snapshot matching the version currently being prepared may precede its
tag; every older snapshot must already have a matching reachable release tag.
