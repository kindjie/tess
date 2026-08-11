# 1.x compatibility snapshots

Create an immutable directory named for the complete release version at RC1
and at every stable minor release. Each directory contains:

- `manifest.json`, recording stable and optional-stable headers, direct stable
  aggregate membership, extracted public symbol names, the consumer source,
  and archive fixture metadata;
- a representative consumer that uses stable headers only; and
- canonical archive-v1 fixtures with producer version and fixed-schema
  metadata, plus one `archive_consumer` source that loads every listed fixture
  when passed the snapshot directory.

`tools/check_compatibility_snapshots.py` verifies that current sources retain
every prior header in its original compatibility class, every direct aggregate
member, and every public symbol, and that fixture files and producer metadata
are complete. Release CI compiles every prior consumer and archive consumer,
then runs the latter against its recorded fixtures and schema. Snapshot
directories are append-only:
never edit a released snapshot to make a compatibility failure disappear.

RC1 and stable `1.x.0` source versions fail the checker until their matching
snapshot exists. Pre-1.0 development does not fabricate a future snapshot.
