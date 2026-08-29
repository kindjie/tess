## 2026-08-28 - Characterize path-strategy crossover envelopes

- **Hypothesis:** cold route caches, shared-goal fields, and weighted batches
  repay setup at platform- and workload-specific request counts that a fixed
  100-request comparison cannot reveal.
- **Controlled change:** added an isolated `lab/` benchmark sweep that pairs
  identical worlds and request arrays across counts and validates status,
  legality, endpoints, and cost outside timing.
- **Evidence:** exact-commit runs completed all 91 primary cells and the
  bounded capacity ladders on an Apple M3 Max and Steam Deck. The public
  [`path-strategy-crossover`](../evidence/v1.0/path-strategy-crossover/README.md)
  record retains the method, normalized samples, counters, environments, and
  controlled stop details. All Deck primary cells met the 5% variation limit;
  40 M3 cells did not and remain excluded from crossover calculation.
- **Result:** room-portal field construction crossed between 10 and 16
  requests on M3 and 4 and 8 on Deck. Deck exact and suffix caches crossed
  between 2 and 8, while M3 showed accepted cache wins by 4 and 8 without an
  accepted lower boundary. One-goal weighted batching crossed between 2 and 4
  on Deck and showed a qualified win by 8 on M3. Eight-goal batching first won
  materially at 10 on both; all-distinct fallback remained inconclusive.
- **Capacity result:** after the 4,096x4,096 preflight showed headroom, the
  opt-in envelope was extended through 16,384x16,384 and 131,072 requests.
  Most Deck grid ladders completed 8,192x8,192 before a controlled 16,384x16,384
  stop; most M3 ladders completed the 16,384x16,384 ceiling. Reuse-heavy
  request ladders reached 131,072 on both, so that figure is a tested floor,
  not a platform maximum.
- **Decision:** keep logically cold population/build work in the measured
  operation while pre-reserving reusable storage, and publish per-platform,
  workload-specific brackets. Keep the campaign advisory and warm replay in
  the existing cache and field-product families. Defer a
  worker-owned multithreaded throughput campaign; require a real GPU path
  provider before claiming GPU pathfinding measurements.
