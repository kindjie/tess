## 2026-08-26 - Congestion findings productize as guide, lab, and one experimental primitive

The congestion-accounting stream (issue #269, six pre-registered
amendment rounds, 28 arms) productizes on three decisions, reviewed by
two independent design agents and chosen by the maintainer:

**Policies stay out of the library.** No pricing policy becomes
library API at any tier: the exploratory evidence is screening-scale,
policy outputs are trajectory-sensitive (a compatibility hazard under
the 1.x promise), and the caller-recipe boundary the v1.0 synthesis
drew remains correct. The recipes ship as a decision-guide page
(`docs/guide/congestion.md`, tier-labelled), a compile-checked
copyable example (`examples/congestion_pricing.cc`, snippet-synced
into the guide), and a browser laboratory
(`examples/web_congestion/`) that wraps the colony tutorial's
simulation through its native seam -- the colony demo itself reverts
to a clean tutorial, and every pricing experiment now lives in the
lab, which the future supported-population matrix will drive as its
exact code path.

**One primitive enters the experimental tier**:
`tess::experimental::request_replans_for_route_crossings`
(`include/tess/experimental/path_agent_replan_selection.h`) -- the
scoped-replanning query behind the stream's largest measured effect
(the recorded case: ~84 ms to ~1.6 ms per tick, about 53x, against
the global-replan protocol). It
grants no authority a caller lacks (it only asks the existing replan
queue), its contract is pinned by direct fixtures, and one contract
detail (whether the scan includes the agent's current tile) is
deliberately left settleable before any stable promotion. This
narrowly supersedes the "no new library authority" entry for
plumbing only; every policy decision remains caller-side.

**The maintained recipe's replanning instruction is corrected**: the
architecture note now prescribes scoped replanning with the global
mark as the historical supported-coverage protocol, ending the
divergence between the documented recipe and the measured best
practice.
