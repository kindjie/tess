## 2026-08-24 - Authority-boundary dispositions (PR X2)

The execution plan reserved one record for capability boundaries the
prototype queue deliberately did not cross, so their absence from v1.0
reads as a decision rather than an omission. Recorded without
speculative implementation:

**Clearance and radius products remain post-1.0.** Capability-specific
topology (per-radius passability, clearance-aware transitions) changes
what a cached field product means, what invalidates it, and what a
movement class is: every consumer of `(chunk_key, content_version)`
dependencies would need a per-capability axis, and the movement-class
algebra would need per-capability composition rules. Nothing in the
v1.0 consumer surface requires it, and the cost lands on the cache and
invalidation contracts that stabilized in v0.12-v0.13.

**Theta\* (any-angle smoothing) stays deferred.** It requires
line-of-sight primitives, a smoothing contract over returned routes,
route-validity semantics under smoothing (what "contiguous face steps"
becomes), and timing evidence -- none of which exist as contracts. The
P-queue's screens also showed the incumbent's fast paths already serve
axis-aligned and detour geometry cheaply; an any-angle surface is a new
public contract, not an optimization.

**Fixed-horizon WHCA remains superseded by the conflict-local gate.**
C3 built the reciprocal-conflict fixtures and proved the production
tier's three failure classes; C4 Phase A resolved all three with
completion-planning escalation bounded by component, never by horizon
-- the queued-yields fixture exists precisely because bounded-horizon
candidates hide exactly that failure mode. The C3 pins and C4's merged
harness are the standing gate any temporal-planning candidate must
pass; a global fixed-horizon planner cannot, by construction.

**Movement-tier planning authority was evaluated and declined on
evidence.** C4's two-phase structure separated mechanism from authority
grant; Phase A's substrate measurement (2 of 61 residual seeds worsen
one agent under trajectory divergence, against an aggregate
improvement) decided against always-on arming, so no public planning
authority ships in v1.0. The promotion blockers are recorded in the C4
evidence: bounded trajectory divergence (or an explicit quality-delta
contract) and incremental reachability for seal prediction.
