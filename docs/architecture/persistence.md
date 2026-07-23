# Persistence and Compatibility

`include/tess/persistence/archive.h` provides a dependency-free, versioned
binary envelope for authoritative world fields. It is a cold-path save/load
primitive, not a live replication stream or a derived-product cache format.

## Archive Schema

Applications define a `PersistenceSchema` with a stable 64-bit application
schema ID, schema version, and ordered `PersistedField` declarations. Each
field declaration assigns a stable 64-bit field ID and field version to a tag
already present in the world's `FieldSchema`. IDs are deliberately explicit:
C++ type names, RTTI values, and addresses are not stable persistence keys.

The initial format supports `bool`, integral, enum, `float`, and `double`
columns whose scalar width is 1, 2, 4, or 8 bytes. Values use canonical
little-endian encoding; floating-point values preserve their IEEE bit pattern.
Unsupported field value types fail at compile time and require an
application-owned scalar field or a later codec extension.

## Envelope and Compatibility

`save_world_archive` writes format version, shape and chunk extents, lattice
identity/version, key-layout version, application schema identity/version,
tess library version, residency kind, field descriptors, and canonical
chunk-key-ordered records. Each record contains the stable chunk lifecycle
state, active flags, entity count, and selected authoritative field columns.
`WorldArchiveResidency` distinguishes dense and sparse envelopes. A CRC-32
covers the complete variable body.

`inspect_world_archive` validates the magic, lengths, checksum, dimensions,
field encodings, canonical unique chunk keys, and complete body before a world
is involved. `load_world_archive` then classifies shape, lattice, key layout,
residency, schema, field, and sparse-capacity compatibility before mutation.
Its `WorldArchiveResult` and `WorldArchiveInfo` retain the source metadata;
`WorldArchiveStatus` distinguishes damage from compatibility decisions.

A differing application schema version returns
`WorldArchiveStatus::MigrationRequired`; it is never silently reinterpreted.
The intended migration is explicit: load with the old `PersistenceSchema` and
old world representation, transform authoritative values in application code,
then save with the new schema. A changed field descriptor under the same
schema version is `FieldMismatch`, which identifies an incorrectly versioned
schema rather than guessing a conversion.

## Loaded State

Successful loads restore selected fields, active flags, stable chunk state,
and entity counts. Dirty history, content/topology version counters, residency
generations, region graphs, paths, products, and caches are intentionally not
serialized. Loading preserves each target chunk's existing version counters
and advances both monotonically, so derived products warmed before an in-place
load cannot become valid again through a reset-to-zero alias. Every loaded
chunk receives a full-chunk topology invalidation; the caller supplies the
dirty mask. Derived state must be rebuilt against the loaded authoritative
fields.

Dense archives contain every shaped chunk. Sparse archives contain the
current resident working set in canonical key order and reject a target whose
fixed capacity cannot hold that set before evicting anything. The current
`SparseResidentWorld` has no non-resident backing store—eviction discards the
page—so the archive cannot invent previously evicted contents. Applications
that own durable chunk streaming must save authoritative chunks before
eviction and coordinate those records with their external store.

## Integrity and Trust Boundary

CRC-32 detects accidental truncation or corruption; it is not authentication
and does not make a hostile archive trustworthy. File size limits, provenance,
signatures, encryption, atomic file replacement, and user/account policy stay
with the application and its I/O layer. tess consumes and produces byte spans
and does not open paths, perform network I/O, or manage credentials.
