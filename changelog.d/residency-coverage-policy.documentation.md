- `integration-policy.md` gains a "Residency coverage" section naming the
  four dense-only families — the queued-operations layer, the weighted
  distance-field products, the portal-route products, and the PIBT tier —
  and stating the concrete change to expect when they absorb sparse: the
  `MissingChunkPolicy` parameter the sparse-aware path entry points
  already carry. All four `static_assert` on `AlwaysResident` with
  messages that incorrectly described sparse support as future work, while
  sitting in the ordinary public namespace, so a consumer had no single place to learn
  which parts of the surface do not compile against a
  `SparseResidentWorld`.
- The audit also found that `weighted_path_batch` *is* residency-generic but
  did not expose its missing-chunk behavior. It now accepts the same explicit
  `MissingChunkPolicy` as the other sparse-aware search entry points.
- The audit offered relocating these families to
  `include/tess/experimental/` as the alternative. That is the wrong half
  of its own either/or: they are production-promoted and tested on dense
  worlds, and the `static_assert`s mark a residency limitation rather than
  experimental status. Relocating would churn every consumer include to
  signal a maturity difference that is not the actual distinction. Being
  outside `experimental/` is explicitly not a stability promise —
  `support.md` makes every `0.x` release pre-stable, and this section says
  so rather than implying otherwise.
