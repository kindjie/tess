## 2026-08-19 - Keep 2D convenience lossless and storage-generic

- `Coord3` remains Tess's canonical world-space representation, while
  `Coord2` converts losslessly to its `z = 0` plane. This lets ordinary
  top-down calls use the shorter type without duplicating every API overload.
  As with any new implicit conversion, unusual downstream overload sets may
  gain another viable candidate; that additive pre-1.0 compatibility risk is
  accepted in exchange for one consistent conversion boundary.
- `Extent3` remains the only extent type. Its existing `z = 1` default already
  expresses a 2D extent as `Extent3{width, height}` without adding a parallel
  shape vocabulary.
- Dense worlds expose `fill_field<Tag>(value)` because every shaped tile is
  resident and “fill the field” is unambiguous. Sparse worlds do not: a
  similarly named operation could either modify only resident pages or
  unexpectedly materialize the complete bounded shape.
- Filling is a direct storage write. Its world traversal allocates no memory,
  although assignment of a user-defined field value may allocate or throw and
  leave a partially assigned field. Like repeated `field()` assignments, it
  does not implicitly alter dirty, active, topology, or content-version
  metadata; simulation-time changes still use the existing explicit
  notification or queued-operation paths.
- Beginner-facing 2D material uses the convenience forms. Architecture,
  persistence, sparse-residency, and genuinely 3D examples retain explicit
  canonical coordinates where those details are part of the lesson.
