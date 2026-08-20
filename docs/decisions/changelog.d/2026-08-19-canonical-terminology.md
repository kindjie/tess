## 2026-08-19 - Canonical terminology defines the 1.0 contract

Tess now treats its domain language as part of the public contract. Ambiguous
raw metadata scalars became explicit mask, version, and residency-generation
types; chunk activity and active-category count derive from the active mask;
and archive format v2 no longer stores contradictory activity state.

Names now state ownership and scope: operation batches are not frames, the
unit route cache retains results rather than acting as scratch, weighted paths
name one movement class, topology build results distinguish a version sum from
a chunk version, and content-version dependencies identify the value they
observe. Sparse searches report an indeterminate result by default when
unknown space prevents a whole-world conclusion, and path agents keep an
optional last search result instead of inventing `NoPath` as lifecycle state.
Default, cleared, stale, and mismatched products instead report `NotComputed`;
bounded or heuristic misses report `NoCandidate`; `NoPath` is reserved for an
authoritative policy-relative search conclusion.
Two-call sparse field readers retain an indeterminate build for every unreached
or non-resident start, while inconsistent derived gradients are `NotComputed`.
Movement validation separates impassable endpoint terrain, blocked
transitions, stale content, and stale topology. Compatibility aliases were not
retained because this pre-1.0 change defines the vocabulary intended for the
1.x line.

The maintained terminology page is the shared human reference, while public
headers and specialized architecture pages remain authoritative for behaviour.
Global hover definitions are limited to phrases whose meaning is unambiguous
on every page; overloaded qualifiers still require visible context.
