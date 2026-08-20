- Establish canonical pre-1.0 terminology across the public API and docs:
  explicit metadata types, derived chunk activity, archive format v2,
  ownership-accurate cache and operation names, explicit weighted movement
  classes, truthful topology-version aggregates, and conservative
  sparse-boundary defaults. Path agents now separate lifecycle phase from an
  optional last search result instead of using `NoPath` as an unsearched
  sentinel. Path products now distinguish `NotComputed`, heuristic
  `NoCandidate`, and authoritative `NoPath` outcomes; two-call sparse fields
  preserve indeterminate builds through later path reads.
