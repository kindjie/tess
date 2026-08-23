## 2026-08-23 - Resumable A\* screen: semantics proven, rejected on scheduling value

**Hypothesis** (pre-registered in issue #240, revision 2 plus a
scope-and-accounting amendment posted before data): a single A\* query can
pause and resume across ticks with caller-owned state without changing its
route, and bounding per-tick search work has scheduling value, defined as
worst-case per-slice heap work actually bounded AND total non-expansion work
growing at most 10% under an accounting where the resumed arm pays every
capture probe and revalidation check the mechanism performs.

**Prototype.** Slice state carried on `PathScratch` behind
`-DTESS_P2_RESUMABLE` (never merged; recorded in the evidence directory).
Single implementation, loop-top slice boundary before any frontier
operation; the dense fast-path preamble is an atomic slice 0; dependency
capture at chunk granularity `(chunk_key, content_version,
residency_generation)` on first read -- preamble reads via a scoped hook on
the passability leaves, heap-loop reads via a bounds-guarded face walk --
revalidated at every resume before any scratch access. Scope: the
orthogonal unit core, dense and sparse; the weighted fallthrough ignores
the slice detectably.

**Semantic gates: all passed.** Byte-identical routes and exact expansion
equality over every slice schedule (44/44 runs, every run verified to have
actually paused -- an earlier probe revision never checked engagement and
could have reported vacuous identity over preamble-answered runs).
Cancellation with state-object reuse; misuse (different request on resume)
aborts under asserts and is refused with recovery in release. Staleness:
version-marked edits of captured chunks detected; edits of
verified-uncaptured chunks do not false-positive; raw unmarked writes
demonstrated undetectable, pinning the declared scope. Sparse residency:
eviction, slot aliasing, same-key rematerialization (content version
restarts at zero; the generation decides), and absent-becomes-resident all
refuse at the resume boundary before any aliased scratch read. Paused
state adds 312 B + 152 B against a 4096-node incumbent scratch; no warm
allocation.

**Scheduling value: rejected.** The per-slice expansion bound holds
everywhere, but the cost bar fails in every configuration: best case
+28.4% (k=64, 64x64 serpentine) against the 10% ceiling, +50.2% at
256x256, and fine slicing is pathological (k=1 at 256x256 pays 19.4x the
baseline's total non-expansion work). The dominant term is per-resume
revalidation, whose size is the captured dependency set; that set grows
with map extent (13 -> 43 -> 151 chunks across two doublings) because the
fast-path preamble genuinely reads along full axis extents before falling
through. The rejection is robust to accounting: charging revalidation
alone still fails at k <= 8 everywhere and at every k on 256x256.

**Decision: reject.** The prototype is removed from the branch; the
evidence directory `docs/planning/evidence/v1.0/p2-resumable/` retains the
recorded prototype diff, the three measurement programs as source, and
their captured outputs. No timing was read, so no two-platform campaign
was required. Reconsideration condition (pre-registered): acceptance of a
future incremental-replanning candidate reopens the question with a
different continuation shape, and can reuse the proven staleness and
residency-revalidation design; the dependency-capture cost structure is
what must change.
