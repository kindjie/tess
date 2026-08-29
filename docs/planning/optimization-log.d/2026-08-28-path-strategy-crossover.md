## 2026-08-28 - Characterize path-strategy crossover envelopes

- **Hypothesis:** cold route caches, shared-goal fields, and weighted batches
  repay setup at platform- and workload-specific request counts that a fixed
  100-request comparison cannot reveal.
- **Controlled change:** added an isolated `lab/` benchmark sweep that pairs
  identical worlds and request arrays across counts and validates status,
  legality, endpoints, and cost outside timing.
- **Evidence status:** pending controlled runs from the committed source. The
  public
  [`path-strategy-crossover`](../evidence/v1.0/path-strategy-crossover/README.md)
  record freezes the reviewed method, environment requirements, work counters,
  and limitations before Apple Silicon and Steam Deck measurements begin.
- **Decision rule:** keep cold setup in the measured operation and publish
  per-platform crossover brackets rather than one universal agent threshold.
  Keep warm replay in the existing cache and field-product families.
- **Limitation and reconsideration:** after a bounded 4,096x4,096 preflight
  showed headroom on both hosts, the opt-in envelope was extended through
  16,384x16,384 and 131,072 requests. It still stops at a declared resource
  budget and must not become a portable timing gate.
