- `integration-policy.md` gains a "Residency coverage" section naming the
  three dense-only families — the queued-operations layer, the weighted
  distance-field products, and the PIBT tier — and carving their sparse
  dimension out of the stability promise. All three `static_assert` on
  `AlwaysResident` with messages saying sparse support "lands later",
  while sitting in the ordinary public namespace, so the promise and the
  code disagreed about what a consumer could rely on.
- The policy states the concrete expected break: absorbing sparse will add
  the `MissingChunkPolicy` parameter every sparse-aware path function
  already carries, defaulted the way `cached_astar_path`'s now is.
- The audit offered relocating these families to
  `include/tess/experimental/` as the alternative. That is the wrong half
  of its own either/or: the families are production-promoted, tested and
  shipped, and the `static_assert`s mark a residency limitation rather
  than experimental status. Relocating would churn every consumer include
  to signal instability that does not exist.
